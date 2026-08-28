#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one match, got {count}")
    p.write_text(text.replace(old, new, 1))


lower = "src/core/core_lower.c"
codegen = "src/target/riscv64/core_codegen.c"

# M118_SIX_OPERAND_ATOMIC_STRUCTURED_ASM: one read/write register, two
# write-only registers, one read/write memory operand and two scalar
# register/immediate inputs. This is a generic GNU extended-asm role family;
# no Linux/futex symbol is encoded here.
anchor = '''    /* M107_STRUCTURED_MEMORY_OUTPUT_ASM: GCC-style asm may pair one\n'''
block = r'''    /* M118_SIX_OPERAND_ATOMIC_STRUCTURED_ASM: preserve a six-operand
       volatile extended-asm shape consisting of one +r register, two =r/=&r
       registers, one +m memory lvalue, and two r/Jr/rJ scalar inputs with a
       compiler memory clobber. Core preserves operand roles; target register
       assignment remains backend-owned. */
    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->outputs != NULL && source->inputs != NULL &&
        source->output_count == 4U && source->input_count == 2U && source->has_memory_clobber &&
        source->label_count == 0U && source->register_clobber_count == 0U &&
        source->clobber_count == 1U) {
        MinicCoreInstruction structured;
        char *numeric_template = NULL;
        size_t numeric_template_length = 0U;
        size_t register_readwrites = 0U;
        size_t register_outputs = 0U;
        size_t memory_readwrites = 0U;
        size_t output_index;
        size_t input_index;
        bool supported_shape = true;

        for (output_index = 0U; output_index < source->output_count; ++output_index) {
            const MinicInlineAsmOperand *operand = &source->outputs[output_index];
            const MinicExpression *output_expression =
                minic_c0_program_expression(context->body->program, operand->expression);
            MinicType value_type;

            if (output_expression == NULL ||
                output_expression->value_category != MINIC_VALUE_LVALUE ||
                minic_type_is_const(output_expression->type) ||
                !minic_type_unqualified(output_expression->type, &value_type) ||
                !core_memory_scalar_type(value_type)) {
                supported_shape = false;
                break;
            }
            if (operand->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE &&
                core_inline_asm_constraint_is(operand, "+r")) {
                if (output_expression->kind == MINIC_EXPRESSION_LOCAL &&
                    minic_c0_program_local_fixed_register_binding(
                        context->body->program, output_expression->value.local_id) != NULL) {
                    supported_shape = false;
                    break;
                }
                register_readwrites += 1U;
            } else if (operand->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY &&
                       core_inline_asm_register_output_constraint(operand)) {
                if (output_expression->kind == MINIC_EXPRESSION_LOCAL &&
                    minic_c0_program_local_fixed_register_binding(
                        context->body->program, output_expression->value.local_id) != NULL) {
                    supported_shape = false;
                    break;
                }
                register_outputs += 1U;
            } else if (operand->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE &&
                       core_inline_asm_constraint_is(operand, "+m")) {
                memory_readwrites += 1U;
            } else {
                supported_shape = false;
                break;
            }
        }
        for (input_index = 0U; supported_shape && input_index < source->input_count;
             ++input_index) {
            const MinicInlineAsmOperand *operand = &source->inputs[input_index];
            const MinicExpression *input_expression =
                minic_c0_program_expression(context->body->program, operand->expression);
            MinicType value_type;

            if (operand->access != MINIC_INLINE_ASM_OPERAND_READ_ONLY ||
                (!core_inline_asm_constraint_is(operand, "Jr") &&
                 !core_inline_asm_constraint_is(operand, "rJ") &&
                 !core_inline_asm_constraint_is(operand, "r")) ||
                input_expression == NULL ||
                !core_scalar_expression_value_type(
                    context->body, input_expression, &value_type) ||
                !core_memory_scalar_type(value_type)) {
                supported_shape = false;
            }
        }
        if (supported_shape && register_readwrites == 1U && register_outputs == 2U &&
            memory_readwrites == 1U &&
            core_inline_asm_numeric_template(source, &numeric_template, &numeric_template_length)) {
            MinicCoreLowerStatus status;
            bool added = minic_core_function_add_opaque_inline_asm(context->function,
                                                                    numeric_template,
                                                                    numeric_template_length,
                                                                    true,
                                                                    true,
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
            structured.value.structured_inline_asm.operand_count = 6U;

            for (output_index = 0U; output_index < source->output_count; ++output_index) {
                const MinicInlineAsmOperand *operand = &source->outputs[output_index];
                MinicCoreStructuredInlineAsmOperand *binding =
                    &structured.value.structured_inline_asm.operands[output_index];

                binding->operand_index = output_index;
                if (operand->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE &&
                    core_inline_asm_constraint_is(operand, "+r")) {
                    binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
                } else if (operand->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY &&
                           core_inline_asm_register_output_constraint(operand)) {
                    binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
                } else {
                    binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
                }
                status = lower_address(context, operand->expression, &binding->value);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
            }
            for (input_index = 0U; input_index < source->input_count; ++input_index) {
                MinicCoreStructuredInlineAsmOperand *binding =
                    &structured.value.structured_inline_asm.operands[4U + input_index];
                binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
                binding->operand_index = 4U + input_index;
                status = lower_expression(
                    context, source->inputs[input_index].expression, &binding->value);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
            }
            return minic_core_function_append_effect_instruction(
                       context->function, context->block_id, &structured)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
        free(numeric_template);
    }

''' + anchor
replace_once(lower, anchor, block, "m118-lowering-anchor")

old = '''          /* M113_MIXED_ATOMIC_STRUCTURED_ASM: generic four-role shape. */
          (register_outputs == 1U && register_readwrites == 1U &&
           memory_outputs == 0U && memory_readwrites == 1U && scalar_inputs == 1U &&
           instruction->value.structured_inline_asm.operand_count == 4U &&
           inline_asm->has_memory_clobber && inline_asm->register_clobber_count == 0U &&
           fixed_bindings == 0U) ||
          (register_outputs == 2U && register_readwrites == 0U &&
'''
new = '''          /* M113_MIXED_ATOMIC_STRUCTURED_ASM: generic four-role shape. */
          (register_outputs == 1U && register_readwrites == 1U &&
           memory_outputs == 0U && memory_readwrites == 1U && scalar_inputs == 1U &&
           instruction->value.structured_inline_asm.operand_count == 4U &&
           inline_asm->has_memory_clobber && inline_asm->register_clobber_count == 0U &&
           fixed_bindings == 0U) ||
          /* M118_SIX_OPERAND_ATOMIC_STRUCTURED_ASM: one read/write register,
             two register outputs, one read/write memory address, two inputs. */
          (register_outputs == 2U && register_readwrites == 1U &&
           memory_outputs == 0U && memory_readwrites == 1U && scalar_inputs == 2U &&
           instruction->value.structured_inline_asm.operand_count == 6U &&
           inline_asm->has_memory_clobber && inline_asm->register_clobber_count == 0U &&
           fixed_bindings == 0U) ||
          (register_outputs == 2U && register_readwrites == 0U &&
'''
replace_once(codegen, old, new, "m118-backend-shape")

old = '''    size_t output_index = 0U;
    size_t memory_index = 0U;
    size_t input_index = 0U;
    size_t binding_index;
    size_t index;

    if (file == NULL || program == NULL || frame == NULL ||
        !core_structured_inline_asm_supported(function, instruction)) {
        return false;
    }
    inline_asm = &function->inline_asms[instruction->value.structured_inline_asm.inline_asm_id];
    for (binding_index = 0U;
'''
new = '''    size_t output_index = 0U;
    size_t memory_index = 0U;
    size_t input_index = 0U;
    size_t generic_register_bindings = 0U;
    size_t binding_index;
    size_t index;

    if (file == NULL || program == NULL || frame == NULL ||
        !core_structured_inline_asm_supported(function, instruction)) {
        return false;
    }
    inline_asm = &function->inline_asms[instruction->value.structured_inline_asm.inline_asm_id];
    /* M118_SIX_OPERAND_ATOMIC_STRUCTURED_ASM: count generic register
       outputs/readwrites before assigning the single memory-address temporary.
       Three register destinations consume t0..t2, so a memory operand for that
       shape must not reuse t2. */
    for (binding_index = 0U;
         binding_index < instruction->value.structured_inline_asm.operand_count;
         ++binding_index) {
        const MinicCoreStructuredInlineAsmOperand *binding =
            &instruction->value.structured_inline_asm.operands[binding_index];
        if (!binding->has_fixed_register_binding &&
            (binding->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
             binding->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE)) {
            generic_register_bindings += 1U;
        }
    }
    for (binding_index = 0U;
'''
replace_once(codegen, old, new, "m118-emitter-precount")

old = '''        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT:
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:
            register_name = memory_registers[memory_index++];
            memory_operand[binding->operand_index] = true;
'''
new = '''        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT:
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:
            if (memory_index >= 1U) {
                return false;
            }
            /* M118_SIX_OPERAND_ATOMIC_STRUCTURED_ASM: t2 is the normal
               memory-address temporary, but a three-register-output shape owns
               t0..t2. Its supported two-input form uses t3/t4, leaving t6 free
               for the memory address without changing older register layouts. */
            register_name = generic_register_bindings >= 3U ? "t6" : memory_registers[memory_index];
            memory_index += 1U;
            memory_operand[binding->operand_index] = true;
'''
replace_once(codegen, old, new, "m118-emitter-memory-register")

print("M118 six-operand atomic structured asm staged")
