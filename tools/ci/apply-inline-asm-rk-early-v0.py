#!/usr/bin/env python3
from pathlib import Path

p = Path("src/core/core_lower_asm.c")
text = p.read_text()

anchor = '''    if (source == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }

    /* M126A_GENERIC_STRUCTURED_ASM: canonical role lowering for register/memory
'''
replacement = '''    if (source == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }

    /* Linux RISC-V CSR helpers use one write-only register output plus an rK
       input and a memory clobber.  M126A below intentionally accepts rK as a
       scalar-register alternative, but doing so first materializes a constant
       input (and the output address) in the frame.  Prefer the immediate
       alternative before generic structured lowering whenever every input can
       be baked into the template.  If specialization fails, M126A retains the
       original register fallback semantics. */
    if (!source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->outputs != NULL && source->inputs != NULL &&
        source->output_count == 1U && source->input_count != 0U &&
        source->label_count == 0U && source->register_clobber_count == 0U &&
        source->clobber_count == (source->has_memory_clobber ? 1U : 0U)) {
        const MinicInlineAsmOperand *output = &source->outputs[0];
        const MinicExpression *output_expression =
            minic_c0_program_expression(context->body->program, output->expression);
        const MinicLocal *local;
        MinicCoreValueId address_id;
        MinicCoreValueId output_value;
        MinicType output_type;
        char *specialized_template = NULL;
        size_t specialized_length = 0U;
        bool register_constraint =
            output->constraint_text != NULL &&
            ((output->constraint_length == 2U &&
              memcmp(output->constraint_text, "=r", 2U) == 0) ||
             (output->constraint_length == 3U &&
              memcmp(output->constraint_text, "=&r", 3U) == 0));

        if (output->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY && register_constraint &&
            output_expression != NULL && output_expression->kind == MINIC_EXPRESSION_LOCAL &&
            output_expression->value_category == MINIC_VALUE_LVALUE &&
            !minic_type_is_const(output_expression->type) &&
            !minic_type_is_volatile(output_expression->type) &&
            minic_type_unqualified(output_expression->type, &output_type) &&
            core_memory_scalar_type(output_type) &&
            core_inline_asm_specialize_register_output_immediates(
                context, source, &specialized_template, &specialized_length)) {
            local = minic_c0_program_local(
                context->body->program, output_expression->value.local_id);
            if (local == NULL) {
                free(specialized_template);
                return MINIC_CORE_LOWER_ERROR;
            }
            if (!local->is_array &&
                minic_c0_program_local_fixed_register_binding(
                    context->body->program, output_expression->value.local_id) == NULL &&
                minic_type_equal(local->type, output_expression->type)) {
                bool added = minic_core_function_add_opaque_inline_asm(
                    context->function,
                    specialized_template,
                    specialized_length,
                    source->is_volatile,
                    source->has_memory_clobber,
                    &inline_asm_id);
                free(specialized_template);
                specialized_template = NULL;
                if (!added) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                (void)memset(&instruction, 0, sizeof(instruction));
                instruction.kind = MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM;
                instruction.span = statement->span;
                instruction.type = output_type;
                instruction.result = MINIC_CORE_VALUE_INVALID;
                instruction.value.inline_asm_id = inline_asm_id;
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &instruction, &output_value)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                if (lower_address(context, output->expression, &address_id) !=
                    MINIC_CORE_LOWER_OK) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                (void)memset(&instruction, 0, sizeof(instruction));
                instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
                instruction.span = statement->span;
                instruction.type = minic_type_void();
                instruction.result = MINIC_CORE_VALUE_INVALID;
                instruction.value.store.address = address_id;
                instruction.value.store.stored_value = output_value;
                instruction.value.store.is_volatile = false;
                return minic_core_function_append_effect_instruction(
                           context->function, context->block_id, &instruction)
                           ? MINIC_CORE_LOWER_OK
                           : MINIC_CORE_LOWER_ERROR;
            }
        }
        free(specialized_template);
    }

    /* M126A_GENERIC_STRUCTURED_ASM: canonical role lowering for register/memory
'''
if text.count(anchor) != 1:
    raise SystemExit("expected one pre-M126A insertion anchor")
text = text.replace(anchor, replacement, 1)
p.write_text(text)
print("MINIC_INLINE_ASM_RK_EARLY_V0=APPLIED")
