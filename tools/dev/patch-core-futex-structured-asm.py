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

# M113_MIXED_ATOMIC_STRUCTURED_ASM: one read/write register, one write-only
# register, one read/write memory operand and one scalar register/immediate
# input. This is a generic GNU extended-asm role combination; Core preserves
# roles and addresses/values while RV64 alone chooses physical registers.
anchor = '''    /* M107_STRUCTURED_MEMORY_OUTPUT_ASM: GCC-style asm may pair one\n'''
block = r'''    /* M113_MIXED_ATOMIC_STRUCTURED_ASM: preserve a four-operand
       volatile extended-asm shape consisting of one +r register, one =r/=&r
       register, one +m memory lvalue, and one r/Jr/rJ scalar input with a
       compiler memory clobber. The operand-role model is already generic; this
       only admits the previously unlisted combination. */
    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->outputs != NULL && source->inputs != NULL &&
        source->output_count == 3U && source->input_count == 1U && source->has_memory_clobber &&
        source->label_count == 0U && source->register_clobber_count == 0U &&
        source->clobber_count == 1U) {
        const MinicInlineAsmOperand *input = &source->inputs[0];
        const MinicExpression *input_expression;
        MinicCoreInstruction structured;
        MinicType input_type;
        char *numeric_template = NULL;
        size_t numeric_template_length = 0U;
        size_t register_readwrite_index = SIZE_MAX;
        size_t register_output_index = SIZE_MAX;
        size_t memory_readwrite_index = SIZE_MAX;
        size_t output_index;
        bool supported_shape = true;

        for (output_index = 0U; output_index < source->output_count; ++output_index) {
            const MinicInlineAsmOperand *operand = &source->outputs[output_index];
            const MinicExpression *expression =
                minic_c0_program_expression(context->body->program, operand->expression);
            MinicType value_type;

            if (expression == NULL || expression->value_category != MINIC_VALUE_LVALUE ||
                minic_type_is_const(expression->type) ||
                !minic_type_unqualified(expression->type, &value_type) ||
                !core_memory_scalar_type(value_type)) {
                supported_shape = false;
                break;
            }
            if (operand->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE &&
                core_inline_asm_constraint_is(operand, "+r")) {
                if (register_readwrite_index != SIZE_MAX ||
                    (expression->kind == MINIC_EXPRESSION_LOCAL &&
                     minic_c0_program_local_fixed_register_binding(
                         context->body->program, expression->value.local_id) != NULL)) {
                    supported_shape = false;
                    break;
                }
                register_readwrite_index = output_index;
            } else if (operand->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY &&
                       core_inline_asm_register_output_constraint(operand)) {
                if (register_output_index != SIZE_MAX ||
                    (expression->kind == MINIC_EXPRESSION_LOCAL &&
                     minic_c0_program_local_fixed_register_binding(
                         context->body->program, expression->value.local_id) != NULL)) {
                    supported_shape = false;
                    break;
                }
                register_output_index = output_index;
            } else if (operand->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE &&
                       core_inline_asm_constraint_is(operand, "+m")) {
                if (memory_readwrite_index != SIZE_MAX) {
                    supported_shape = false;
                    break;
                }
                memory_readwrite_index = output_index;
            } else {
                supported_shape = false;
                break;
            }
        }
        input_expression = minic_c0_program_expression(context->body->program, input->expression);
        if (!supported_shape || register_readwrite_index == SIZE_MAX ||
            register_output_index == SIZE_MAX || memory_readwrite_index == SIZE_MAX ||
            input->access != MINIC_INLINE_ASM_OPERAND_READ_ONLY ||
            (!core_inline_asm_constraint_is(input, "Jr") &&
             !core_inline_asm_constraint_is(input, "rJ") &&
             !core_inline_asm_constraint_is(input, "r")) ||
            input_expression == NULL ||
            !core_scalar_expression_value_type(context->body, input_expression, &input_type) ||
            !core_memory_scalar_type(input_type)) {
            supported_shape = false;
        }
        if (supported_shape && core_inline_asm_numeric_template(
                source, &numeric_template, &numeric_template_length)) {
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
            structured.value.structured_inline_asm.operand_count = 4U;

            for (output_index = 0U; output_index < source->output_count; ++output_index) {
                MinicCoreStructuredInlineAsmOperand *binding =
                    &structured.value.structured_inline_asm.operands[output_index];
                binding->operand_index = output_index;
                binding->kind = output_index == register_readwrite_index
                                    ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE
                                : output_index == register_output_index
                                    ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
                                    : MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
                status = lower_address(
                    context, source->outputs[output_index].expression, &binding->value);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
            }
            structured.value.structured_inline_asm.operands[3].kind =
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
            structured.value.structured_inline_asm.operands[3].operand_index = 3U;
            status = lower_expression(
                context, input->expression, &structured.value.structured_inline_asm.operands[3].value);
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

''' + anchor
replace_once(lower, anchor, block, "m113-lowering-anchor")

old = '''          (register_outputs == 2U && register_readwrites == 0U &&
           memory_readwrites == 1U && scalar_inputs <= 2U &&
           scalar_inputs + 3U == instruction->value.structured_inline_asm.operand_count &&
           inline_asm->has_memory_clobber) ||
'''
new = '''          /* M113_MIXED_ATOMIC_STRUCTURED_ASM: generic four-role shape. */
          (register_outputs == 1U && register_readwrites == 1U &&
           memory_outputs == 0U && memory_readwrites == 1U && scalar_inputs == 1U &&
           instruction->value.structured_inline_asm.operand_count == 4U &&
           inline_asm->has_memory_clobber && inline_asm->register_clobber_count == 0U &&
           fixed_bindings == 0U) ||
          (register_outputs == 2U && register_readwrites == 0U &&
           memory_readwrites == 1U && scalar_inputs <= 2U &&
           scalar_inputs + 3U == instruction->value.structured_inline_asm.operand_count &&
           inline_asm->has_memory_clobber) ||
'''
replace_once(codegen, old, new, "m113-backend-shape")

print("M113 mixed atomic structured asm staged")
