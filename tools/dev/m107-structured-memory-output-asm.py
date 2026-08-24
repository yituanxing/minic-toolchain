#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    p = Path(path)
    s = p.read_text()
    count = s.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, got {count}")
    p.write_text(s.replace(old, new, 1))


lower = Path("src/core/core_lower.c")
if "M107_STRUCTURED_MEMORY_OUTPUT_ASM" in lower.read_text():
    print("M107 already applied")
    raise SystemExit(0)

replace_once(
    "src/core/core_ir.h",
    """    MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE,\n    MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE,\n    MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT\n""",
    """    MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE,\n    /* M107_STRUCTURED_MEMORY_OUTPUT_ASM: address-backed write-only memory\n       operand (`=m`). Keep this distinct from read/write memory (`+m`/`+A`). */\n    MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT,\n    MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE,\n    MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT\n""",
    "Core structured memory-output operand kind",
)

replace_once(
    "src/core/core_ir.c",
    """            case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:\n            case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE:\n            case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:\n""",
    """            case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:\n            case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE:\n            case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT:\n            case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:\n""",
    "Core verifier memory-output address operand",
)

lower_anchor = """    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&\n        source->template_length != 0U && source->outputs != NULL && source->inputs != NULL &&\n        source->output_count == 2U && source->input_count == 1U && source->has_memory_clobber &&\n        source->label_count == 0U && source->register_clobber_count == 0U &&\n        source->clobber_count == 1U) {\n"""

lower_block = r'''    /* M107_STRUCTURED_MEMORY_OUTPUT_ASM: GCC-style asm may pair one
       register read/write output with one write-only memory output and a
       scalar register/immediate input. Preserve those access roles in Core;
       target register allocation and template interpretation remain backend-owned. */
    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->outputs != NULL && source->inputs != NULL &&
        source->output_count == 2U && source->input_count == 1U &&
        source->label_count == 0U && source->register_clobber_count == 0U &&
        !source->has_memory_clobber && source->clobber_count == 0U) {
        const MinicInlineAsmOperand *input = &source->inputs[0];
        const MinicInlineAsmOperand *memory_output = NULL;
        const MinicInlineAsmOperand *register_output = NULL;
        const MinicExpression *input_expression;
        const MinicExpression *memory_expression;
        const MinicExpression *register_expression;
        const MinicLocal *register_local;
        MinicCoreInstruction structured;
        MinicType input_type;
        MinicType memory_type;
        MinicType register_type;
        char *numeric_template = NULL;
        size_t numeric_template_length = 0U;
        size_t memory_index = SIZE_MAX;
        size_t register_index = SIZE_MAX;
        size_t output_index;
        bool supported_shape = true;

        for (output_index = 0U; output_index < source->output_count; ++output_index) {
            const MinicInlineAsmOperand *candidate = &source->outputs[output_index];

            if (candidate->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE &&
                core_inline_asm_constraint_is(candidate, "+r")) {
                if (register_output != NULL) {
                    supported_shape = false;
                    break;
                }
                register_output = candidate;
                register_index = output_index;
            } else if (candidate->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY &&
                       core_inline_asm_constraint_is(candidate, "=m")) {
                if (memory_output != NULL) {
                    supported_shape = false;
                    break;
                }
                memory_output = candidate;
                memory_index = output_index;
            } else {
                supported_shape = false;
                break;
            }
        }

        input_expression = minic_c0_program_expression(context->body->program, input->expression);
        memory_expression = memory_output == NULL
                                ? NULL
                                : minic_c0_program_expression(context->body->program,
                                                              memory_output->expression);
        register_expression = register_output == NULL
                                  ? NULL
                                  : minic_c0_program_expression(context->body->program,
                                                                register_output->expression);
        register_local = register_expression == NULL ||
                                 register_expression->kind != MINIC_EXPRESSION_LOCAL
                             ? NULL
                             : minic_c0_program_local(context->body->program,
                                                      register_expression->value.local_id);
        if (!supported_shape || register_output == NULL || memory_output == NULL ||
            register_index == SIZE_MAX || memory_index == SIZE_MAX || input_expression == NULL ||
            memory_expression == NULL || register_expression == NULL || register_local == NULL ||
            input->access != MINIC_INLINE_ASM_OPERAND_READ_ONLY ||
            (!core_inline_asm_constraint_is(input, "rJ") &&
             !core_inline_asm_constraint_is(input, "r")) ||
            register_expression->value_category != MINIC_VALUE_LVALUE ||
            memory_expression->value_category != MINIC_VALUE_LVALUE ||
            minic_type_is_const(register_expression->type) ||
            minic_type_is_volatile(register_expression->type) ||
            minic_type_is_const(memory_expression->type) || register_local->is_array ||
            minic_c0_program_local_fixed_register_binding(
                context->body->program, register_expression->value.local_id) != NULL ||
            !minic_type_equal(register_local->type, register_expression->type) ||
            !minic_type_unqualified(register_expression->type, &register_type) ||
            !minic_type_unqualified(memory_expression->type, &memory_type) ||
            !core_memory_scalar_type(register_type) || !core_memory_scalar_type(memory_type) ||
            !core_scalar_expression_value_type(context->body, input_expression, &input_type) ||
            !core_memory_scalar_type(input_type)) {
            supported_shape = false;
        }

        if (supported_shape &&
            core_inline_asm_numeric_template(
                source, &numeric_template, &numeric_template_length)) {
            MinicCoreInlineAsmId inline_asm_id;
            MinicCoreLowerStatus status;

            if (!minic_core_function_add_opaque_inline_asm(context->function,
                                                           numeric_template,
                                                           numeric_template_length,
                                                           source->is_volatile,
                                                           source->has_memory_clobber,
                                                           &inline_asm_id)) {
                free(numeric_template);
                return MINIC_CORE_LOWER_ERROR;
            }
            free(numeric_template);
            numeric_template = NULL;
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
                binding->kind = output_index == register_index
                                    ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE
                                    : MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT;
                status = lower_address(context, operand->expression, &binding->value);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
            }
            structured.value.structured_inline_asm.operands[2].kind =
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
            structured.value.structured_inline_asm.operands[2].operand_index = 2U;
            status = lower_expression(
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
replace_once(
    "src/core/core_lower.c",
    lower_anchor,
    lower_block + lower_anchor,
    "Core M107 structured asm lowering insertion",
)

replace_once(
    "src/target/riscv64/core_codegen.c",
    """    size_t register_outputs = 0U;\n    size_t register_readwrites = 0U;\n    size_t memory_readwrites = 0U;\n    size_t scalar_inputs = 0U;\n""",
    """    size_t register_outputs = 0U;\n    size_t register_readwrites = 0U;\n    size_t memory_outputs = 0U;\n    size_t memory_readwrites = 0U;\n    size_t scalar_inputs = 0U;\n""",
    "RV64 structured asm memory-output count",
)

replace_once(
    "src/target/riscv64/core_codegen.c",
    """        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:\n            if (!minic_type_pointee(function->values[binding->value].type, &pointee) ||\n                !minic_type_unqualified(pointee, &value_type) || !core_scalar_type(value_type)) {\n                return false;\n            }\n            memory_readwrites += 1U;\n            break;\n""",
    """        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT:\n            if (!minic_type_pointee(function->values[binding->value].type, &pointee) ||\n                !minic_type_unqualified(pointee, &value_type) || !core_scalar_type(value_type)) {\n                return false;\n            }\n            memory_outputs += 1U;\n            break;\n        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:\n            if (!minic_type_pointee(function->values[binding->value].type, &pointee) ||\n                !minic_type_unqualified(pointee, &value_type) || !core_scalar_type(value_type)) {\n                return false;\n            }\n            memory_readwrites += 1U;\n            break;\n""",
    "RV64 structured asm memory-output verification",
)

replace_once(
    "src/target/riscv64/core_codegen.c",
    """    if (!((register_outputs == 2U && register_readwrites == 0U &&\n""",
    """    if (memory_outputs != 0U &&\n        !(register_outputs == 0U && register_readwrites == 1U && memory_outputs == 1U &&\n          memory_readwrites == 0U && scalar_inputs == 1U &&\n          instruction->value.structured_inline_asm.operand_count == 3U &&\n          !inline_asm->has_memory_clobber && inline_asm->register_clobber_count == 0U &&\n          fixed_bindings == 0U)) {\n        return false;\n    }\n    if (!((register_outputs == 0U && register_readwrites == 1U && memory_outputs == 1U &&\n           memory_readwrites == 0U && scalar_inputs == 1U &&\n           instruction->value.structured_inline_asm.operand_count == 3U &&\n           !inline_asm->has_memory_clobber && inline_asm->register_clobber_count == 0U &&\n           fixed_bindings == 0U) ||\n          (register_outputs == 2U && register_readwrites == 0U &&\n""",
    "RV64 structured asm M107 shape whitelist",
)

replace_once(
    "src/target/riscv64/core_codegen.c",
    """        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:\n            register_name = memory_registers[memory_index++];\n""",
    """        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT:\n        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:\n            register_name = memory_registers[memory_index++];\n""",
    "RV64 structured asm memory-output emission",
)

test = Path("tests/core/run-core-ir-shadow.sh")
ts = test.read_text()
anchor = """cat >\"$work_dir/fixed-register-sbi-ecall.i\" <<'EOF'\n"""
case = r'''cat >"$work_dir/register-readwrite-memory-output-asm.i" <<'EOF'
long core_put_user_shape(int *p, int x) {
    long err = 0;
    __asm__ __volatile__(
        "1:\n\t"
        "sw %z2, %1\n"
        "2:\n"
        : "+r" (err), "=m" (*p)
        : "rJ" (x));
    return err;
}
EOF
check_strict_case register-readwrite-memory-output-asm

'''
if ts.count(anchor) != 1:
    raise SystemExit(f"core shadow insertion anchor count={ts.count(anchor)}")
test.write_text(ts.replace(anchor, case + anchor, 1))

print("M107 structured memory-output asm Core support applied")
