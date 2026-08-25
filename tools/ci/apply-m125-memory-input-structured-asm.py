#!/usr/bin/env python3
from pathlib import Path


def replace_once(path_text: str, old: str, new: str) -> None:
    path = Path(path_text)
    source = path.read_text()
    count = source.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one anchor, got {count}")
    path.write_text(source.replace(old, new, 1))


# Core IR owns the semantic distinction between write-only/read-write memory
# operands and a read-only memory input. The latter is address-backed but may
# legally point at const storage and never causes a post-asm writeback.
replace_once(
    "src/core/core_ir.h",
    """    MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT,\n    MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE,\n    MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT\n""",
    """    MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT,\n    /* M125_STRUCTURED_MEMORY_INPUT_ASM: read-only `m` operands carry an\n       address into Core, but unlike output/read-write memory they permit const\n       pointees and never require post-asm writeback. */\n    MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT,\n    MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE,\n    MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT\n""",
)

replace_once(
    "src/core/core_ir.c",
    """            case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:\n            case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE:\n            case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT:\n            case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:\n                if (!available_pointer_pointee(\n                        function, available_values, binding->value, &pointee) ||\n                    minic_type_is_const(pointee) ||\n                    !minic_type_unqualified(pointee, &value_type) ||\n                    (!minic_type_is_integer(value_type) && !minic_type_is_pointer(value_type))) {\n                    return false;\n                }\n                if (binding->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE) {\n                    has_memory_readwrite = true;\n                }\n                break;\n""",
    """            case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:\n            case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE:\n            case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT:\n            case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT:\n            case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:\n                if (!available_pointer_pointee(\n                        function, available_values, binding->value, &pointee) ||\n                    (binding->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT &&\n                     minic_type_is_const(pointee)) ||\n                    !minic_type_unqualified(pointee, &value_type) ||\n                    (!minic_type_is_integer(value_type) && !minic_type_is_pointer(value_type))) {\n                    return false;\n                }\n                if (binding->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE) {\n                    has_memory_readwrite = true;\n                }\n                break;\n""",
)

# Lower the common Linux trap/uaccess shape generically by operand role. This
# deliberately keys on semantic constraints/access modes, not template text or
# function names.
core_lower_anchor = """    /* M107_STRUCTURED_MEMORY_OUTPUT_ASM: GCC-style asm may pair one\n       register read/write output with one write-only memory output and a\n       scalar register/immediate input. Preserve those access roles in Core;\n       target register allocation and template interpretation remain backend-owned. */\n"""
core_lower_block = r'''    /* M125_STRUCTURED_MEMORY_INPUT_ASM: one register read/write output,
       one write-only register output, and one read-only memory input. `m` is
       address-backed in Core; the backend materializes only its address and
       never writes the referenced object after the asm. */
    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->outputs != NULL && source->inputs != NULL &&
        source->output_count == 2U && source->input_count == 1U &&
        source->label_count == 0U && source->register_clobber_count == 0U &&
        !source->has_memory_clobber && source->clobber_count == 0U) {
        const MinicInlineAsmOperand *input = &source->inputs[0];
        const MinicExpression *input_expression =
            minic_c0_program_expression(context->body->program, input->expression);
        MinicCoreInstruction structured;
        MinicType input_type;
        char *numeric_template = NULL;
        size_t numeric_template_length = 0U;
        size_t output_index;
        size_t register_output_count = 0U;
        size_t register_readwrite_count = 0U;
        bool supported_shape = true;

        if (input->access != MINIC_INLINE_ASM_OPERAND_READ_ONLY ||
            !core_inline_asm_constraint_is(input, "m") || input_expression == NULL ||
            input_expression->value_category != MINIC_VALUE_LVALUE ||
            !minic_type_unqualified(input_expression->type, &input_type) ||
            !core_memory_scalar_type(input_type)) {
            supported_shape = false;
        }
        for (output_index = 0U; supported_shape && output_index < source->output_count;
             ++output_index) {
            const MinicInlineAsmOperand *operand = &source->outputs[output_index];
            const MinicExpression *expression =
                minic_c0_program_expression(context->body->program, operand->expression);
            MinicType value_type;

            if (expression == NULL || expression->value_category != MINIC_VALUE_LVALUE ||
                minic_type_is_const(expression->type) ||
                !minic_type_unqualified(expression->type, &value_type) ||
                !core_memory_scalar_type(value_type) ||
                (expression->kind == MINIC_EXPRESSION_LOCAL &&
                 minic_c0_program_local_fixed_register_binding(
                     context->body->program, expression->value.local_id) != NULL)) {
                supported_shape = false;
                break;
            }
            if (operand->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE &&
                core_inline_asm_constraint_is(operand, "+r")) {
                register_readwrite_count += 1U;
            } else if (operand->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY &&
                       core_inline_asm_register_output_constraint(operand)) {
                register_output_count += 1U;
            } else {
                supported_shape = false;
            }
        }
        if (supported_shape && register_readwrite_count == 1U && register_output_count == 1U &&
            core_inline_asm_numeric_template(
                source, &numeric_template, &numeric_template_length)) {
            MinicCoreLowerStatus status;
            bool added;

            added = minic_core_function_add_opaque_inline_asm(context->function,
                                                               numeric_template,
                                                               numeric_template_length,
                                                               source->is_volatile,
                                                               source->has_memory_clobber,
                                                               &inline_asm_id);
            free(numeric_template);
            numeric_template = NULL;
            if (!added) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&structured, 0, sizeof(structured));
            structured.kind = MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM;
            structured.span = statement->span;
            structured.type = minic_type_void();
            structured.result = MINIC_CORE_VALUE_INVALID;
            structured.value.structured_inline_asm.inline_asm_id = inline_asm_id;
            structured.value.structured_inline_asm.operand_count = 3U;

            for (output_index = 0U; output_index < source->output_count; ++output_index) {
                const MinicInlineAsmOperand *operand = &source->outputs[output_index];
                MinicCoreStructuredInlineAsmOperand *binding =
                    &structured.value.structured_inline_asm.operands[output_index];

                binding->operand_index = output_index;
                binding->kind = operand->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE
                                    ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE
                                    : MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
                status = lower_address(context, operand->expression, &binding->value);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
            }
            structured.value.structured_inline_asm.operands[2].kind =
                MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
            structured.value.structured_inline_asm.operands[2].operand_index = 2U;
            status = lower_address(
                context, input->expression, &structured.value.structured_inline_asm.operands[2].value);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            return minic_core_function_append_effect_instruction(
                       context->function, context->block_id, &structured)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
        free(numeric_template);
    }

'''
replace_once("src/core/core_lower.c", core_lower_anchor, core_lower_block + core_lower_anchor)

# RV64 validation remains capability-based: admit the new semantic role and a
# resource-safe 1 read/write register + 1 output register + 1 memory-address
# family. The emitter already separates register outputs from memory-address
# temporaries; M125 makes the read-only memory role participate in that path.
replace_once(
    "src/target/riscv64/core_codegen.c",
    """    size_t memory_outputs = 0U;\n    size_t memory_readwrites = 0U;\n    size_t scalar_inputs = 0U;\n""",
    """    size_t memory_outputs = 0U;\n    size_t memory_inputs = 0U;\n    size_t memory_readwrites = 0U;\n    size_t scalar_inputs = 0U;\n""",
)

replace_once(
    "src/target/riscv64/core_codegen.c",
    """        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT:\n            if (!minic_type_pointee(function->values[binding->value].type, &pointee) ||\n                !minic_type_unqualified(pointee, &value_type) || !core_scalar_type(value_type)) {\n                return false;\n            }\n            memory_outputs += 1U;\n            break;\n        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:\n""",
    """        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT:\n            if (!minic_type_pointee(function->values[binding->value].type, &pointee) ||\n                !minic_type_unqualified(pointee, &value_type) || !core_scalar_type(value_type)) {\n                return false;\n            }\n            memory_outputs += 1U;\n            break;\n        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT:\n            if (!minic_type_pointee(function->values[binding->value].type, &pointee) ||\n                !minic_type_unqualified(pointee, &value_type) || !core_scalar_type(value_type)) {\n                return false;\n            }\n            memory_inputs += 1U;\n            break;\n        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:\n""",
)

whitelist_anchor = """          /* M113_MIXED_ATOMIC_STRUCTURED_ASM: generic four-role shape. */\n          (register_outputs == 1U && register_readwrites == 1U &&\n"""
whitelist_insert = """          /* M125_STRUCTURED_MEMORY_INPUT_ASM: Linux trap/uaccess family.\n             The read-only memory input owns an address register but no writeback. */\n          (register_outputs == 1U && register_readwrites == 1U && memory_outputs == 0U &&\n           memory_inputs == 1U && memory_readwrites == 0U && scalar_inputs == 0U &&\n           instruction->value.structured_inline_asm.operand_count == 3U &&\n           !inline_asm->has_memory_clobber && inline_asm->register_clobber_count == 0U &&\n           fixed_bindings == 0U) ||\n"""
replace_once("src/target/riscv64/core_codegen.c", whitelist_anchor, whitelist_insert + whitelist_anchor)

replace_once(
    "src/target/riscv64/core_codegen.c",
    """        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT:\n        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:\n            if (memory_index >= 1U) {\n""",
    """        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT:\n        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT:\n        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:\n            if (memory_index >= 1U) {\n""",
)

# Make check-fast carry a strict Core regression for the exact semantic family.
replace_once(
    "tests/compiler/c0/run-gnu-inline-asm-operands.sh",
    """grep -F 'lw t1, 0(t3)' \"$assembly\" >/dev/null\ngrep -F 'addi t3, zero, 7' \"$assembly\" >/dev/null\n""",
    """grep -F 'lw t1, 0(t3)' \"$assembly\" >/dev/null\n\ncat >\"$work/core-memory-input.c\" <<'EOF'\nstatic int core_memory_input_linux_shape(const int *value) {\n    long error = 0;\n    int loaded;\n\n    __asm__ __volatile__(\"lw %1, %2\" : \"+r\"(error), \"=&r\"(loaded) : \"m\"(*value));\n    return loaded + (int)error;\n}\nEOF\n\"$host_cc\" -E -P -std=gnu11 -x c \"$work/core-memory-input.c\" \\\n    -o \"$work/core-memory-input.i\"\nMINIC_CORE_IR=strict \"$minic\" -S \"$work/core-memory-input.i\" \\\n    -o \"$work/core-memory-input.s\"\ngrep -F '.type core_memory_input_linux_shape, @function' \"$work/core-memory-input.s\" >/dev/null\ngrep -F 'lw t1, (t2)' \"$work/core-memory-input.s\" >/dev/null\n\ngrep -F 'addi t3, zero, 7' \"$assembly\" >/dev/null\n""",
)

print("M125 structured read-only memory input staged")
