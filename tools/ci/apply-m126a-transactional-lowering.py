#!/usr/bin/env python3
from pathlib import Path

path = Path("src/core/core_lower.c")
source = path.read_text()
start_marker = "    /* M126A_GENERIC_STRUCTURED_ASM: canonical role lowering"
end_marker = "\n    if (core_inline_asm_single_label_goto_supported(context, source)) {"
begin = source.find(start_marker)
if begin < 0:
    raise SystemExit("M126A generic lowering marker not found")
end = source.find(end_marker, begin)
if end < 0:
    raise SystemExit("M126A generic lowering end marker not found")

replacement = r'''    /* M126A_GENERIC_STRUCTURED_ASM: canonical role lowering for register/memory
       extended asm. Preflight is deliberately side-effect free: an asm that
       ultimately belongs to an older/specialized path must not leave partial
       Core values, objects, or instructions behind. Only after every operand
       role and the numeric template are proven do we materialize operands. */
    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->label_count == 0U &&
        source->output_count <= MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT &&
        source->input_count <= MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT - source->output_count &&
        source->output_count + source->input_count != 0U &&
        (source->output_count == 0U || source->outputs != NULL) &&
        (source->input_count == 0U || source->inputs != NULL) &&
        source->clobber_count == source->register_clobber_count +
                                     (source->has_memory_clobber ? 1U : 0U)) {
        MinicCoreInstruction structured;
        char *numeric_template = NULL;
        size_t numeric_template_length = 0U;
        size_t output_index;
        size_t input_index;
        bool supported_shape = true;

        (void)memset(&structured, 0, sizeof(structured));
        structured.kind = MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM;
        structured.span = statement->span;
        structured.type = minic_type_void();
        structured.result = MINIC_CORE_VALUE_INVALID;
        structured.value.structured_inline_asm.operand_count =
            source->output_count + source->input_count;

        /* Phase 1: pure classification only. No Core mutation is permitted. */
        for (output_index = 0U; output_index < source->output_count; ++output_index) {
            const MinicInlineAsmOperand *operand = &source->outputs[output_index];
            const MinicExpression *expression =
                minic_c0_program_expression(context->body->program, operand->expression);
            MinicCoreStructuredInlineAsmOperand *binding =
                &structured.value.structured_inline_asm.operands[output_index];
            MinicType value_type;
            size_t fixed_binding_id;

            if (expression == NULL || expression->value_category != MINIC_VALUE_LVALUE ||
                minic_type_is_const(expression->type) ||
                !minic_type_unqualified(expression->type, &value_type) ||
                !core_memory_scalar_type(value_type)) {
                supported_shape = false;
                break;
            }
            binding->operand_index = output_index;
            binding->early_clobber =
                operand->constraint_text != NULL &&
                memchr(operand->constraint_text, '&', operand->constraint_length) != NULL;
            if (operand->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY &&
                (core_inline_asm_constraint_is(operand, "=r") ||
                 core_inline_asm_constraint_is(operand, "=&r"))) {
                binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
            } else if (operand->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE &&
                       (core_inline_asm_constraint_is(operand, "+r") ||
                        core_inline_asm_constraint_is(operand, "+&r"))) {
                binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
            } else if (operand->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY &&
                       core_inline_asm_constraint_is(operand, "=m")) {
                binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT;
                binding->early_clobber = false;
            } else if (operand->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE &&
                       (core_inline_asm_constraint_is(operand, "+m") ||
                        core_inline_asm_constraint_is(operand, "+A"))) {
                binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
                binding->early_clobber = false;
            } else {
                supported_shape = false;
                break;
            }
            if ((binding->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
                 binding->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE) &&
                core_inline_asm_local_fixed_binding_id(
                    context->body->program, expression, &fixed_binding_id)) {
                binding->fixed_register_binding_id = fixed_binding_id;
                binding->has_fixed_register_binding = true;
            }
        }

        for (input_index = 0U; supported_shape && input_index < source->input_count;
             ++input_index) {
            const MinicInlineAsmOperand *operand = &source->inputs[input_index];
            const MinicExpression *expression =
                minic_c0_program_expression(context->body->program, operand->expression);
            size_t operand_index = source->output_count + input_index;
            MinicCoreStructuredInlineAsmOperand *binding =
                &structured.value.structured_inline_asm.operands[operand_index];
            MinicType value_type;
            size_t fixed_binding_id;

            if (operand->access != MINIC_INLINE_ASM_OPERAND_READ_ONLY || expression == NULL) {
                supported_shape = false;
                break;
            }
            binding->operand_index = operand_index;
            if (core_inline_asm_constraint_is(operand, "m")) {
                if (expression->value_category != MINIC_VALUE_LVALUE ||
                    !minic_type_unqualified(expression->type, &value_type) ||
                    !core_memory_scalar_type(value_type)) {
                    supported_shape = false;
                    break;
                }
                binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
            } else if (core_inline_asm_constraint_is(operand, "r") ||
                       core_inline_asm_constraint_is(operand, "rJ") ||
                       core_inline_asm_constraint_is(operand, "Jr") ||
                       core_inline_asm_constraint_is(operand, "rK")) {
                if (!core_scalar_expression_value_type(context->body, expression, &value_type) ||
                    !core_memory_scalar_type(value_type)) {
                    supported_shape = false;
                    break;
                }
                binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
                if (core_inline_asm_local_fixed_binding_id(
                        context->body->program, expression, &fixed_binding_id)) {
                    binding->fixed_register_binding_id = fixed_binding_id;
                    binding->has_fixed_register_binding = true;
                }
            } else {
                supported_shape = false;
                break;
            }
        }

        /* Template normalization is also part of preflight. A failed probe
           falls through with the Core function exactly unchanged. */
        if (supported_shape && core_inline_asm_numeric_template(
                source, &numeric_template, &numeric_template_length)) {
            MinicCoreLowerStatus status;
            size_t clobber_index;

            /* Phase 2: commit operand materialization. Any failure from here
               aborts this function lowering, so partial state is destroyed by
               minic_core_lower_function rather than leaking into another path. */
            for (output_index = 0U; output_index < source->output_count; ++output_index) {
                MinicCoreStructuredInlineAsmOperand *binding =
                    &structured.value.structured_inline_asm.operands[output_index];
                status = lower_address(
                    context, source->outputs[output_index].expression, &binding->value);
                if (status != MINIC_CORE_LOWER_OK) {
                    free(numeric_template);
                    return status;
                }
            }
            for (input_index = 0U; input_index < source->input_count; ++input_index) {
                size_t operand_index = source->output_count + input_index;
                MinicCoreStructuredInlineAsmOperand *binding =
                    &structured.value.structured_inline_asm.operands[operand_index];
                if (binding->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT) {
                    status = lower_address(
                        context, source->inputs[input_index].expression, &binding->value);
                } else {
                    status = lower_expression(
                        context, source->inputs[input_index].expression, &binding->value);
                }
                if (status != MINIC_CORE_LOWER_OK) {
                    free(numeric_template);
                    return status;
                }
            }

            if (!minic_core_function_add_opaque_inline_asm(context->function,
                                                            numeric_template,
                                                            numeric_template_length,
                                                            true,
                                                            source->has_memory_clobber,
                                                            &inline_asm_id)) {
                free(numeric_template);
                return MINIC_CORE_LOWER_ERROR;
            }
            free(numeric_template);
            numeric_template = NULL;
            for (clobber_index = 0U; clobber_index < source->register_clobber_count;
                 ++clobber_index) {
                const MinicInlineAsmRegisterClobber *clobber =
                    &source->register_clobbers[clobber_index];
                if (clobber->name == NULL || clobber->name_length == 0U ||
                    !minic_core_function_add_inline_asm_register_clobber(
                        context->function,
                        inline_asm_id,
                        clobber->name,
                        clobber->name_length)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
            }
            structured.value.structured_inline_asm.inline_asm_id = inline_asm_id;
            return minic_core_function_append_effect_instruction(
                       context->function, context->block_id, &structured)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
        free(numeric_template);
    }
'''

path.write_text(source[:begin] + replacement + source[end:])
print("M126A transactional structured lowering staged")
