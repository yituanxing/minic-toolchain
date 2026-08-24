#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, name: str) -> None:
    p = Path(path)
    text = p.read_text()
    if new in text:
        return
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"Batch I {name} anchor count={count}")
    p.write_text(text.replace(old, new, 1))


path = "src/core/core_lower.c"

# Register-output asm may still have compile-time-only input operands. Resolve
# those inputs at the Core boundary while preserving operand 0 as the single
# runtime register output. Keep this deliberately narrow: one output, numeric
# %0..%9 references, no GNU print modifiers, and all input operands must resolve
# through the existing i/I immediate seam.
helper_anchor = '''/* M67_STRUCTURED_MULTI_OPERAND_INLINE_ASM: normalize GNU named operand\n   references to Core's compact numeric operand indices. Constraint semantics\n   stay at the lowering boundary; Core itself only retains operand roles. */\n'''
helper = r'''/* BATCH_I_REGISTER_OUTPUT_IMMEDIATE_SPECIALIZATION: a value-producing
   asm may have one runtime register output plus compile-time-only immediate
   inputs. Preserve %0 for the output and bake %1..%9 into target text using
   the existing i/I constant/symbol resolver. No target instruction meaning is
   introduced into Core. */
static bool core_inline_asm_specialize_register_output_immediates(
    const MinicCoreLowerContext *context,
    const MinicInlineAsm *source,
    char **template_out,
    size_t *template_length_out) {
    char integer_text[MINIC_CORE_IMMEDIATE_ASM_LIMIT][MINIC_CORE_IMMEDIATE_TEXT_LIMIT];
    const char *replacements[MINIC_CORE_IMMEDIATE_ASM_LIMIT];
    size_t replacement_lengths[MINIC_CORE_IMMEDIATE_ASM_LIMIT];
    size_t input_index;
    size_t cursor;
    size_t output_length;
    size_t output_cursor;
    char *specialized;

    if (context == NULL || source == NULL || template_out == NULL ||
        template_length_out == NULL || source->template_text == NULL ||
        source->template_length == 0U || source->output_count != 1U ||
        source->input_count == 0U || source->input_count > 9U ||
        source->input_count > MINIC_CORE_IMMEDIATE_ASM_LIMIT || source->inputs == NULL) {
        return false;
    }
    for (input_index = 0U; input_index < source->input_count; ++input_index) {
        if (!core_inline_asm_immediate_text(context,
                                            &source->inputs[input_index],
                                            integer_text[input_index],
                                            sizeof(integer_text[input_index]),
                                            &replacements[input_index],
                                            &replacement_lengths[input_index])) {
            return false;
        }
    }

    cursor = 0U;
    output_length = 0U;
    while (cursor < source->template_length) {
        size_t operand_index;

        if (source->template_text[cursor] != '%') {
            if (output_length == SIZE_MAX) return false;
            output_length += 1U;
            cursor += 1U;
            continue;
        }
        if (cursor + 1U >= source->template_length) return false;
        if (source->template_text[cursor + 1U] == '%') {
            if (output_length == SIZE_MAX) return false;
            output_length += 1U;
            cursor += 2U;
            continue;
        }
        if (source->template_text[cursor + 1U] < '0' ||
            source->template_text[cursor + 1U] > '9') {
            return false;
        }
        operand_index = (size_t)(source->template_text[cursor + 1U] - '0');
        if (operand_index == 0U) {
            if (output_length > SIZE_MAX - 2U) return false;
            output_length += 2U;
        } else {
            input_index = operand_index - 1U;
            if (input_index >= source->input_count ||
                output_length > SIZE_MAX - replacement_lengths[input_index]) {
                return false;
            }
            output_length += replacement_lengths[input_index];
        }
        cursor += 2U;
    }
    if (output_length == SIZE_MAX) return false;
    specialized = (char *)malloc(output_length + 1U);
    if (specialized == NULL) return false;

    cursor = 0U;
    output_cursor = 0U;
    while (cursor < source->template_length) {
        size_t operand_index;

        if (source->template_text[cursor] != '%') {
            specialized[output_cursor++] = source->template_text[cursor++];
            continue;
        }
        if (source->template_text[cursor + 1U] == '%') {
            specialized[output_cursor++] = '%';
            cursor += 2U;
            continue;
        }
        operand_index = (size_t)(source->template_text[cursor + 1U] - '0');
        if (operand_index == 0U) {
            specialized[output_cursor++] = '%';
            specialized[output_cursor++] = '0';
        } else {
            input_index = operand_index - 1U;
            (void)memcpy(specialized + output_cursor,
                         replacements[input_index],
                         replacement_lengths[input_index]);
            output_cursor += replacement_lengths[input_index];
        }
        cursor += 2U;
    }
    specialized[output_cursor] = '\0';
    if (output_cursor != output_length) {
        free(specialized);
        return false;
    }
    *template_out = specialized;
    *template_length_out = output_length;
    return true;
}

'''
replace_once(path, helper_anchor, helper + helper_anchor, "specializer-helper")

lower_anchor = '''    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&\n        source->template_length != 0U && source->outputs != NULL &&\n        source->output_count == 1U && source->input_count == 0U &&\n        source->label_count == 0U && source->register_clobber_count == 0U &&\n        source->clobber_count == (source->has_memory_clobber ? 1U : 0U)) {\n'''
lower = r'''    /* BATCH_I_REGISTER_OUTPUT_IMMEDIATE_SPECIALIZATION: after all i/I
       inputs are baked into the template, the runtime shape is exactly the
       existing one-register-output instruction. Core has no optimizer that can
       discard value-producing asm, so retain the specialized instruction in the
       existing execution-effect table; this does not add source-level volatile
       semantics or target-specific IR. */
    if (!source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->outputs != NULL && source->inputs != NULL &&
        source->output_count == 1U && source->input_count != 0U &&
        source->label_count == 0U && source->register_clobber_count == 0U &&
        source->clobber_count == 0U && !source->has_memory_clobber) {
        const MinicInlineAsmOperand *output;
        const MinicExpression *output_expression;
        const MinicLocal *local;
        MinicCoreValueId address_id;
        MinicCoreValueId output_value;
        MinicType output_type;
        char *specialized_template;
        size_t specialized_length;
        bool register_constraint;

        output = &source->outputs[0];
        output_expression = minic_c0_program_expression(context->body->program, output->expression);
        register_constraint =
            output->constraint_text != NULL &&
            ((output->constraint_length == 2U &&
              memcmp(output->constraint_text, "=r", 2U) == 0) ||
             (output->constraint_length == 3U &&
              memcmp(output->constraint_text, "=&r", 3U) == 0));
        specialized_template = NULL;
        specialized_length = 0U;
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
                /* The specialized text contains only the runtime output %0 and
                   literal %% escapes. Retain it as an execution effect because
                   its SSA result is semantically required. */
                if (!minic_core_function_add_opaque_inline_asm(context->function,
                                                               specialized_template,
                                                               specialized_length,
                                                               true,
                                                               false,
                                                               &inline_asm_id)) {
                    free(specialized_template);
                    return MINIC_CORE_LOWER_ERROR;
                }
                free(specialized_template);
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
                if (lower_address(context, output->expression, &address_id) != MINIC_CORE_LOWER_OK) {
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

'''
replace_once(path, lower_anchor, lower + lower_anchor, "register-output-immediates")

print("CORE_BATCH_I_PATCHED register-output immediate specialization")
