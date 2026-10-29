#include "core/core_lower_asm_internal.h"

#include "frontend/const_eval.h"
#include "frontend/expression_semantics.h"
#include "target/data_layout.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Ordered scalar, immediate, read/write and memory GNU-asm effect patterns.
 * Operand decoding and the ordered driver are owned by core_lower_asm.c. */

/* Ordered GNU-asm shape admission: the first committed case wins.
 * Unsupported shapes fall through without claiming ownership. */
MinicCoreLowerStatus core_asm_try_atomic_and_immediate_forms(
    MinicCoreLowerContext *context, const MinicStatement *statement,
    const MinicInlineAsm *source, bool *handled) {
    *handled = false;
    MinicCoreInlineAsmId inline_asm_id;
    MinicCoreInstruction instruction;
    if (!source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->output_count == 0U &&
        source->input_count != 0U && source->inputs != NULL && source->label_count == 0U &&
        source->register_clobber_count == 0U && !source->has_memory_clobber &&
        source->clobber_count == 0U) {
        char *specialized_template;
        size_t specialized_length;

        specialized_template = NULL;
        specialized_length = 0U;
        if (core_inline_asm_specialize_immediates(
                context, source, &specialized_template, &specialized_length)) {
            bool added;

            added = specialized_length != 0U &&
                    minic_core_function_add_opaque_inline_asm(context->function,
                                                              specialized_template,
                                                              specialized_length,
                                                              true,
                                                              false,
                                                              &inline_asm_id);
            free(specialized_template);
            if (!added) {
                { *handled = true; return MINIC_CORE_LOWER_ERROR; }
            }
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM;
            instruction.span = statement->span;
            instruction.type = minic_type_void();
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.inline_asm_id = inline_asm_id;
            { *handled = true; return minic_core_function_append_effect_instruction(
                       context->function, context->block_id, &instruction)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR; }
        }
    }

    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length == 0U && source->output_count == 0U &&
        source->input_count == 0U && source->label_count == 0U &&
        source->register_clobber_count == 0U && source->has_memory_clobber &&
        source->clobber_count == 1U) {
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_COMPILER_BARRIER;
        instruction.span = statement->span;
        instruction.type = minic_type_void();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        { *handled = true; return minic_core_function_append_effect_instruction(
                   context->function, context->block_id, &instruction)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR; }
    }

    /* Earlier role-based structured ASM admission subsumes the former
     * M113/M118/M125/M107 shape-specialized handlers. They were unreachable
     * after the canonical preflight claimed every matching GNU role/constraint
     * combination, and duplicated effect construction without a new contract.
     * Keep only genuinely distinct scalar/immediate lowering here. */
    return MINIC_CORE_LOWER_OK;
}

/* Ordered GNU-asm shape admission: the first committed case wins.
 * Unsupported shapes fall through without claiming ownership. */
MinicCoreLowerStatus core_asm_try_mixed_memory_forms(
    MinicCoreLowerContext *context, const MinicStatement *statement,
    const MinicInlineAsm *source, bool *handled) {
    *handled = false;
    MinicCoreInlineAsmId inline_asm_id;
    MinicCoreInstruction instruction;
    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->outputs != NULL && source->inputs != NULL &&
        source->output_count == 2U && source->input_count == 1U && source->has_memory_clobber &&
        source->label_count == 0U && source->register_clobber_count == 0U &&
        source->clobber_count == 1U) {
        const MinicInlineAsmOperand *input;
        const MinicInlineAsmOperand *memory_output;
        const MinicInlineAsmOperand *register_output;
        const MinicExpression *input_expression;
        const MinicExpression *memory_expression;
        const MinicExpression *register_expression;
        const MinicLocal *register_local;
        MinicCoreValueId input_value;
        MinicCoreValueId memory_address;
        MinicCoreValueId output_address;
        MinicCoreValueId output_value;
        MinicCoreLowerStatus status;
        MinicType input_type;
        MinicType memory_type;
        MinicType output_type;
        size_t memory_index;
        size_t register_index;

        input = &source->inputs[0];
        memory_output = NULL;
        register_output = NULL;
        memory_index = SIZE_MAX;
        register_index = SIZE_MAX;
        for (size_t output_index = 0U; output_index < 2U; ++output_index) {
            const MinicInlineAsmOperand *candidate;

            candidate = &source->outputs[output_index];
            if (candidate->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE &&
                core_inline_asm_constraint_is(candidate, "+A")) {
                if (memory_output != NULL) {
                    { *handled = true; return MINIC_CORE_LOWER_UNSUPPORTED; }
                }
                memory_output = candidate;
                memory_index = output_index;
            } else if (candidate->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY &&
                       core_inline_asm_register_output_constraint(candidate)) {
                if (register_output != NULL) {
                    { *handled = true; return MINIC_CORE_LOWER_UNSUPPORTED; }
                }
                register_output = candidate;
                register_index = output_index;
            } else {
                memory_output = NULL;
                register_output = NULL;
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
        if (memory_output != NULL && register_output != NULL &&
            input->access == MINIC_INLINE_ASM_OPERAND_READ_ONLY &&
            core_inline_asm_constraint_is(input, "r") && input_expression != NULL &&
            memory_expression != NULL && register_expression != NULL &&
            memory_expression->value_category == MINIC_VALUE_LVALUE &&
            register_expression->kind == MINIC_EXPRESSION_LOCAL &&
            register_expression->value_category == MINIC_VALUE_LVALUE &&
            !minic_type_is_const(memory_expression->type) &&
            !minic_type_is_const(register_expression->type) &&
            !minic_type_is_volatile(register_expression->type) &&
            minic_type_unqualified(memory_expression->type, &memory_type) &&
            minic_type_unqualified(register_expression->type, &output_type) &&
            core_memory_scalar_type(memory_type) && core_memory_scalar_type(output_type) &&
            core_scalar_expression_value_type(context->body, input_expression, &input_type) &&
            minic_type_equal(memory_type, input_type) && minic_type_equal(output_type, memory_type)) {
            register_local = minic_c0_program_local(
                context->body->program, register_expression->value.local_id);
            if (register_local == NULL) {
                { *handled = true; return MINIC_CORE_LOWER_ERROR; }
            }
            if (!register_local->is_array &&
                minic_c0_program_local_fixed_register_binding(
                    context->body->program, register_expression->value.local_id) == NULL &&
                minic_type_equal(register_local->type, register_expression->type)) {
                status = lower_expression(context, input->expression, &input_value);
                if (status != MINIC_CORE_LOWER_OK) {
                    { *handled = true; return status; }
                }
                status = lower_address(context, memory_output->expression, &memory_address);
                if (status != MINIC_CORE_LOWER_OK) {
                    { *handled = true; return status; }
                }
                if (input_value >= context->function->value_count ||
                    memory_address >= context->function->value_count ||
                    !minic_type_equal(context->function->values[input_value].type, input_type)) {
                    { *handled = true; return MINIC_CORE_LOWER_ERROR; }
                }
                if (!minic_core_function_add_opaque_inline_asm(context->function,
                                                               source->template_text,
                                                               source->template_length,
                                                               source->is_volatile,
                                                               source->has_memory_clobber,
                                                               &inline_asm_id)) {
                    { *handled = true; return MINIC_CORE_LOWER_ERROR; }
                }
                (void)memset(&instruction, 0, sizeof(instruction));
                instruction.kind = MINIC_CORE_INSTRUCTION_MEMORY_READWRITE_SCALAR_INPUT_INLINE_ASM;
                instruction.span = statement->span;
                instruction.type = output_type;
                instruction.result = MINIC_CORE_VALUE_INVALID;
                instruction.value.memory_readwrite_scalar_input_inline_asm.inline_asm_id =
                    inline_asm_id;
                instruction.value.memory_readwrite_scalar_input_inline_asm.memory_address =
                    memory_address;
                instruction.value.memory_readwrite_scalar_input_inline_asm.operand = input_value;
                instruction.value.memory_readwrite_scalar_input_inline_asm.memory_operand_index =
                    memory_index;
                instruction.value.memory_readwrite_scalar_input_inline_asm.register_output_operand_index =
                    register_index;
                instruction.value.memory_readwrite_scalar_input_inline_asm.scalar_input_operand_index =
                    2U;
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &instruction, &output_value)) {
                    { *handled = true; return MINIC_CORE_LOWER_ERROR; }
                }
                status = lower_address(context, register_output->expression, &output_address);
                if (status != MINIC_CORE_LOWER_OK) {
                    { *handled = true; return status; }
                }
                (void)memset(&instruction, 0, sizeof(instruction));
                instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
                instruction.span = statement->span;
                instruction.type = minic_type_void();
                instruction.result = MINIC_CORE_VALUE_INVALID;
                instruction.value.store.address = output_address;
                instruction.value.store.stored_value = output_value;
                instruction.value.store.is_volatile = false;
                {
                    bool stored = minic_core_function_append_effect_instruction(
                        context->function, context->block_id, &instruction);
                    if (getenv("MINIC_ASM_BATCH_TRACE") != NULL) {
                        (void)fprintf(stderr,
                                      "ASM_OUTPUT_STAGE function=%s stage=store-output status=%s "
                                      "block=%u instructions=%zu values=%zu objects=%zu\n",
                                      context->source_function != NULL &&
                                              context->source_function->name != NULL
                                          ? context->source_function->name
                                          : "?",
                                      stored ? "OK" : "ERROR",
                                      (unsigned int)context->block_id,
                                      context->function->instruction_count,
                                      context->function->value_count,
                                      context->function->object_count);
                    }
                    { *handled = true; return stored ? MINIC_CORE_LOWER_OK : MINIC_CORE_LOWER_ERROR; }
                }
            }
        }
    }

    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->outputs != NULL && source->inputs != NULL &&
        source->output_count == 1U && source->input_count == 1U && source->has_memory_clobber &&
        source->label_count == 0U && source->register_clobber_count == 0U &&
        source->clobber_count == 1U) {
        const MinicInlineAsmOperand *input;
        const MinicInlineAsmOperand *memory_output;
        const MinicExpression *input_expression;
        const MinicExpression *memory_expression;
        MinicCoreValueId input_value;
        MinicCoreValueId memory_address;
        MinicCoreLowerStatus status;
        MinicType input_type;
        MinicType memory_type;

        memory_output = &source->outputs[0];
        input = &source->inputs[0];
        memory_expression =
            minic_c0_program_expression(context->body->program, memory_output->expression);
        input_expression = minic_c0_program_expression(context->body->program, input->expression);
        if (memory_output->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE &&
            input->access == MINIC_INLINE_ASM_OPERAND_READ_ONLY &&
            core_inline_asm_constraint_is(memory_output, "+A") &&
            core_inline_asm_constraint_is(input, "r") && memory_expression != NULL &&
            input_expression != NULL && memory_expression->value_category == MINIC_VALUE_LVALUE &&
            !minic_type_is_const(memory_expression->type) &&
            minic_type_unqualified(memory_expression->type, &memory_type) &&
            core_memory_scalar_type(memory_type) &&
            core_scalar_expression_value_type(context->body, input_expression, &input_type) &&
            minic_type_equal(memory_type, input_type)) {
            status = lower_expression(context, input->expression, &input_value);
            if (status != MINIC_CORE_LOWER_OK) {
                { *handled = true; return status; }
            }
            status = lower_address(context, memory_output->expression, &memory_address);
            if (status != MINIC_CORE_LOWER_OK) {
                { *handled = true; return status; }
            }
            if (input_value >= context->function->value_count ||
                memory_address >= context->function->value_count ||
                !minic_type_equal(context->function->values[input_value].type, input_type)) {
                { *handled = true; return MINIC_CORE_LOWER_ERROR; }
            }
            if (!minic_core_function_add_opaque_inline_asm(context->function,
                                                           source->template_text,
                                                           source->template_length,
                                                           source->is_volatile,
                                                           source->has_memory_clobber,
                                                           &inline_asm_id)) {
                { *handled = true; return MINIC_CORE_LOWER_ERROR; }
            }
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_MEMORY_READWRITE_SCALAR_INPUT_INLINE_ASM;
            instruction.span = statement->span;
            instruction.type = minic_type_void();
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.memory_readwrite_scalar_input_inline_asm.inline_asm_id =
                inline_asm_id;
            instruction.value.memory_readwrite_scalar_input_inline_asm.memory_address =
                memory_address;
            instruction.value.memory_readwrite_scalar_input_inline_asm.operand = input_value;
            instruction.value.memory_readwrite_scalar_input_inline_asm.memory_operand_index = 0U;
            instruction.value.memory_readwrite_scalar_input_inline_asm.register_output_operand_index =
                SIZE_MAX;
            instruction.value.memory_readwrite_scalar_input_inline_asm.scalar_input_operand_index =
                1U;
            { *handled = true; return minic_core_function_append_effect_instruction(
                       context->function, context->block_id, &instruction)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR; }
        }
    }

    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->outputs != NULL && source->inputs != NULL &&
        source->output_count == 1U && source->input_count == 1U &&
        source->label_count == 0U && source->register_clobber_count == 0U &&
        source->clobber_count == (source->has_memory_clobber ? 1U : 0U)) {
        const MinicInlineAsmOperand *input;
        const MinicInlineAsmOperand *output;
        const MinicExpression *input_expression;
        const MinicExpression *output_expression;
        const MinicLocal *local;
        MinicCoreValueId address_id;
        MinicCoreValueId input_value;
        MinicCoreValueId output_value;
        MinicCoreLowerStatus status;
        MinicType input_type;
        MinicType output_type;
        bool input_register_constraint;
        bool output_register_constraint;

        output = &source->outputs[0];
        input = &source->inputs[0];
        output_expression = minic_c0_program_expression(context->body->program, output->expression);
        input_expression = minic_c0_program_expression(context->body->program, input->expression);
        output_register_constraint =
            output->constraint_text != NULL &&
            ((output->constraint_length == 2U &&
              memcmp(output->constraint_text, "=r", 2U) == 0) ||
             (output->constraint_length == 3U &&
              memcmp(output->constraint_text, "=&r", 3U) == 0));
        input_register_constraint =
            input->constraint_text != NULL &&
            ((input->constraint_length == 1U && memcmp(input->constraint_text, "r", 1U) == 0) ||
             (input->constraint_length == 2U && memcmp(input->constraint_text, "rK", 2U) == 0));
        if (output->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY &&
            input->access == MINIC_INLINE_ASM_OPERAND_READ_ONLY &&
            output_register_constraint && input_register_constraint &&
            output_expression != NULL && output_expression->kind == MINIC_EXPRESSION_LOCAL &&
            output_expression->value_category == MINIC_VALUE_LVALUE &&
            !minic_type_is_const(output_expression->type) &&
            !minic_type_is_volatile(output_expression->type) &&
            minic_type_unqualified(output_expression->type, &output_type) &&
            core_memory_scalar_type(output_type) && input_expression != NULL &&
            core_scalar_expression_value_type(context->body, input_expression, &input_type)) {
            local = minic_c0_program_local(
                context->body->program, output_expression->value.local_id);
            if (local == NULL) {
                { *handled = true; return MINIC_CORE_LOWER_ERROR; }
            }
            if (!local->is_array &&
                minic_c0_program_local_fixed_register_binding(
                    context->body->program, output_expression->value.local_id) == NULL &&
                minic_type_equal(local->type, output_expression->type)) {
                if (core_inline_asm_constraint_is(input, "rK")) {
                    char *specialized_template = NULL;
                    size_t specialized_length = 0U;
                    if (core_inline_asm_specialize_register_output_immediates(
                            context, source, &specialized_template, &specialized_length)) {
                        bool added = minic_core_function_add_opaque_inline_asm(
                            context->function,
                            specialized_template,
                            specialized_length,
                            source->is_volatile,
                            source->has_memory_clobber,
                            &inline_asm_id);
                        free(specialized_template);
                        if (!added) {
                            { *handled = true; return MINIC_CORE_LOWER_ERROR; }
                        }
                        (void)memset(&instruction, 0, sizeof(instruction));
                        instruction.kind = MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM;
                        instruction.span = statement->span;
                        instruction.type = output_type;
                        instruction.result = MINIC_CORE_VALUE_INVALID;
                        instruction.value.inline_asm_id = inline_asm_id;
                        if (!minic_core_function_append_value_instruction(
                                context->function,
                                context->block_id,
                                &instruction,
                                &output_value)) {
                            { *handled = true; return MINIC_CORE_LOWER_ERROR; }
                        }
                        if (lower_address(context, output->expression, &address_id) !=
                            MINIC_CORE_LOWER_OK) {
                            { *handled = true; return MINIC_CORE_LOWER_ERROR; }
                        }
                        (void)memset(&instruction, 0, sizeof(instruction));
                        instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
                        instruction.span = statement->span;
                        instruction.type = minic_type_void();
                        instruction.result = MINIC_CORE_VALUE_INVALID;
                        instruction.value.store.address = address_id;
                        instruction.value.store.stored_value = output_value;
                        instruction.value.store.is_volatile = false;
                        { *handled = true; return minic_core_function_append_effect_instruction(
                                   context->function, context->block_id, &instruction)
                                   ? MINIC_CORE_LOWER_OK
                                   : MINIC_CORE_LOWER_ERROR; }
                    }
                    free(specialized_template);
                }
                status = lower_expression(context, input->expression, &input_value);
                if (status != MINIC_CORE_LOWER_OK) {
                    { *handled = true; return status; }
                }
                if (input_value >= context->function->value_count ||
                    !minic_type_equal(context->function->values[input_value].type, input_type)) {
                    { *handled = true; return MINIC_CORE_LOWER_ERROR; }
                }
                if (!minic_core_function_add_opaque_inline_asm(context->function,
                                                               source->template_text,
                                                               source->template_length,
                                                               source->is_volatile,
                                                               source->has_memory_clobber,
                                                               &inline_asm_id)) {
                    { *handled = true; return MINIC_CORE_LOWER_ERROR; }
                }
                (void)memset(&instruction, 0, sizeof(instruction));
                instruction.kind = MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INPUT_INLINE_ASM;
                instruction.span = statement->span;
                instruction.type = output_type;
                instruction.result = MINIC_CORE_VALUE_INVALID;
                instruction.value.register_output_input_inline_asm.inline_asm_id = inline_asm_id;
                instruction.value.register_output_input_inline_asm.operand = input_value;
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &instruction, &output_value)) {
                    { *handled = true; return MINIC_CORE_LOWER_ERROR; }
                }
                if (lower_address(context, output->expression, &address_id) != MINIC_CORE_LOWER_OK) {
                    { *handled = true; return MINIC_CORE_LOWER_ERROR; }
                }
                (void)memset(&instruction, 0, sizeof(instruction));
                instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
                instruction.span = statement->span;
                instruction.type = minic_type_void();
                instruction.result = MINIC_CORE_VALUE_INVALID;
                instruction.value.store.address = address_id;
                instruction.value.store.stored_value = output_value;
                instruction.value.store.is_volatile = false;
                {
                    bool stored = minic_core_function_append_effect_instruction(
                        context->function, context->block_id, &instruction);
                    if (getenv("MINIC_ASM_BATCH_TRACE") != NULL) {
                        (void)fprintf(stderr,
                                      "ASM_OUTPUT_STAGE function=%s stage=store-output status=%s "
                                      "block=%u instructions=%zu values=%zu objects=%zu\n",
                                      context->source_function != NULL &&
                                              context->source_function->name != NULL
                                          ? context->source_function->name
                                          : "?",
                                      stored ? "OK" : "ERROR",
                                      (unsigned int)context->block_id,
                                      context->function->instruction_count,
                                      context->function->value_count,
                                      context->function->object_count);
                    }
                    { *handled = true; return stored ? MINIC_CORE_LOWER_OK : MINIC_CORE_LOWER_ERROR; }
                }
            }
        }
    }

    return MINIC_CORE_LOWER_OK;
}

/* Ordered GNU-asm shape admission: the first committed case wins.
 * Unsupported shapes fall through without claiming ownership. */
MinicCoreLowerStatus core_asm_try_readwrite_and_output_specialization(
    MinicCoreLowerContext *context, const MinicStatement *statement,
    const MinicInlineAsm *source, bool *handled) {
    *handled = false;
    MinicCoreInlineAsmId inline_asm_id;
    MinicCoreInstruction instruction;
    /* BATCH_L_STRUCTURED_REGISTER_READWRITE: after compile-time i/I inputs
       are specialized into target text, preserve a +r operand as one
       address-backed read/write register binding. Register-clobber names stay
       opaque in Core and are interpreted only by the target backend when it
       chooses operand registers. */
    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->outputs != NULL && source->inputs != NULL &&
        source->output_count == 1U && source->input_count != 0U &&
        source->label_count == 0U && !source->has_memory_clobber &&
        source->clobber_count == source->register_clobber_count) {
        const MinicInlineAsmOperand *output = &source->outputs[0];
        const MinicExpression *output_expression =
            minic_c0_program_expression(context->body->program, output->expression);
        MinicType output_type;
        char *specialized_template = NULL;
        size_t specialized_length = 0U;

        if (output->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE &&
            core_inline_asm_constraint_is(output, "+r") &&
            output_expression != NULL &&
            output_expression->value_category == MINIC_VALUE_LVALUE &&
            !minic_type_is_const(output_expression->type) &&
            minic_type_unqualified(output_expression->type, &output_type) &&
            core_memory_scalar_type(output_type) &&
            core_inline_asm_specialize_register_output_immediates(
                context, source, &specialized_template, &specialized_length)) {
            MinicCoreInstruction structured;
            MinicCoreStructuredInlineAsmOperand *binding;
            MinicCoreLowerStatus status;
            size_t clobber_index;
            bool added;

            added = minic_core_function_add_opaque_inline_asm(context->function,
                                                               specialized_template,
                                                               specialized_length,
                                                               true,
                                                               false,
                                                               &inline_asm_id);
            free(specialized_template);
            if (!added) {
                { *handled = true; return MINIC_CORE_LOWER_ERROR; }
            }
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
                    { *handled = true; return MINIC_CORE_LOWER_ERROR; }
                }
            }

            (void)memset(&structured, 0, sizeof(structured));
            structured.kind = MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM;
            structured.span = statement->span;
            structured.type = minic_type_void();
            structured.result = MINIC_CORE_VALUE_INVALID;
            structured.value.structured_inline_asm.inline_asm_id = inline_asm_id;
            structured.value.structured_inline_asm.operand_count = 1U;
            binding = &structured.value.structured_inline_asm.operands[0];
            binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
            binding->operand_index = 0U;
            status = lower_address(context, output->expression, &binding->value);
            if (status != MINIC_CORE_LOWER_OK) {
                { *handled = true; return status; }
            }
            { *handled = true; return minic_core_function_append_effect_instruction(
                       context->function, context->block_id, &structured)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR; }
        }
        free(specialized_template);
    }

    /* BATCH_I_REGISTER_OUTPUT_IMMEDIATE_SPECIALIZATION: after all i/I
       inputs are baked into the template, the runtime shape is exactly the
       existing one-register-output instruction. Core has no optimizer that can
       discard value-producing asm, so retain the specialized instruction in the
       existing execution-effect table; this does not add source-level volatile
       semantics or target-specific IR. */
    if (!source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->outputs != NULL && source->inputs != NULL &&
        source->output_count == 1U && source->input_count != 0U &&
        source->label_count == 0U && source->register_clobber_count == 0U &&
        source->clobber_count == (source->has_memory_clobber ? 1U : 0U)) {
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
                { *handled = true; return MINIC_CORE_LOWER_ERROR; }
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
                                                               source->has_memory_clobber,
                                                               &inline_asm_id)) {
                    free(specialized_template);
                    { *handled = true; return MINIC_CORE_LOWER_ERROR; }
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
                    { *handled = true; return MINIC_CORE_LOWER_ERROR; }
                }
                if (lower_address(context, output->expression, &address_id) != MINIC_CORE_LOWER_OK) {
                    { *handled = true; return MINIC_CORE_LOWER_ERROR; }
                }
                (void)memset(&instruction, 0, sizeof(instruction));
                instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
                instruction.span = statement->span;
                instruction.type = minic_type_void();
                instruction.result = MINIC_CORE_VALUE_INVALID;
                instruction.value.store.address = address_id;
                instruction.value.store.stored_value = output_value;
                instruction.value.store.is_volatile = false;
                {
                    bool stored = minic_core_function_append_effect_instruction(
                        context->function, context->block_id, &instruction);
                    if (getenv("MINIC_ASM_BATCH_TRACE") != NULL) {
                        (void)fprintf(stderr,
                                      "ASM_OUTPUT_STAGE function=%s stage=store-output status=%s "
                                      "block=%u instructions=%zu values=%zu objects=%zu\n",
                                      context->source_function != NULL &&
                                              context->source_function->name != NULL
                                          ? context->source_function->name
                                          : "?",
                                      stored ? "OK" : "ERROR",
                                      (unsigned int)context->block_id,
                                      context->function->instruction_count,
                                      context->function->value_count,
                                      context->function->object_count);
                    }
                    { *handled = true; return stored ? MINIC_CORE_LOWER_OK : MINIC_CORE_LOWER_ERROR; }
                }
            }
        }
        free(specialized_template);
    }

    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->outputs != NULL &&
        source->output_count == 1U && source->input_count == 0U &&
        source->label_count == 0U && source->register_clobber_count == 0U &&
        source->clobber_count == (source->has_memory_clobber ? 1U : 0U)) {
        const MinicInlineAsmOperand *output;
        const MinicExpression *output_expression;
        const MinicLocal *local;
        MinicCoreValueId address_id;
        MinicCoreValueId output_value;
        MinicType output_type;
        bool register_constraint;

        output = &source->outputs[0];
        output_expression = minic_c0_program_expression(context->body->program, output->expression);
        register_constraint =
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
            core_memory_scalar_type(output_type)) {
            local = minic_c0_program_local(
                context->body->program, output_expression->value.local_id);
            if (local == NULL) {
                { *handled = true; return MINIC_CORE_LOWER_ERROR; }
            }
            if (!local->is_array &&
                minic_c0_program_local_fixed_register_binding(
                    context->body->program, output_expression->value.local_id) == NULL &&
                minic_type_equal(local->type, output_expression->type)) {
                if (!minic_core_function_add_opaque_inline_asm(context->function,
                                                               source->template_text,
                                                               source->template_length,
                                                               source->is_volatile,
                                                               source->has_memory_clobber,
                                                               &inline_asm_id)) {
                    { *handled = true; return MINIC_CORE_LOWER_ERROR; }
                }
                (void)memset(&instruction, 0, sizeof(instruction));
                instruction.kind = MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM;
                instruction.span = statement->span;
                instruction.type = output_type;
                instruction.result = MINIC_CORE_VALUE_INVALID;
                instruction.value.inline_asm_id = inline_asm_id;
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &instruction, &output_value)) {
                    { *handled = true; return MINIC_CORE_LOWER_ERROR; }
                }
                if (lower_address(context, output->expression, &address_id) != MINIC_CORE_LOWER_OK) {
                    { *handled = true; return MINIC_CORE_LOWER_ERROR; }
                }
                (void)memset(&instruction, 0, sizeof(instruction));
                instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
                instruction.span = statement->span;
                instruction.type = minic_type_void();
                instruction.result = MINIC_CORE_VALUE_INVALID;
                instruction.value.store.address = address_id;
                instruction.value.store.stored_value = output_value;
                instruction.value.store.is_volatile = false;
                {
                    bool stored = minic_core_function_append_effect_instruction(
                        context->function, context->block_id, &instruction);
                    if (getenv("MINIC_ASM_BATCH_TRACE") != NULL) {
                        (void)fprintf(stderr,
                                      "ASM_OUTPUT_STAGE function=%s stage=store-output status=%s "
                                      "block=%u instructions=%zu values=%zu objects=%zu\n",
                                      context->source_function != NULL &&
                                              context->source_function->name != NULL
                                          ? context->source_function->name
                                          : "?",
                                      stored ? "OK" : "ERROR",
                                      (unsigned int)context->block_id,
                                      context->function->instruction_count,
                                      context->function->value_count,
                                      context->function->object_count);
                    }
                    { *handled = true; return stored ? MINIC_CORE_LOWER_OK : MINIC_CORE_LOWER_ERROR; }
                }
            }
        }
    }

    return MINIC_CORE_LOWER_OK;
}

/* Ordered GNU-asm shape admission: the first committed case wins.
 * Unsupported shapes fall through without claiming ownership. */
MinicCoreLowerStatus core_asm_try_empty_barrier_scalar_forms(
    MinicCoreLowerContext *context, const MinicStatement *statement,
    const MinicInlineAsm *source, bool *handled) {
    *handled = false;
    MinicCoreInlineAsmId inline_asm_id;
    MinicCoreInstruction instruction;
    /* Linux BUG/BUG_ON uses asm volatile("") immediately before
     * __builtin_unreachable(). An empty volatile template has no ISA
     * instruction or operand effect, but unlike its nonvolatile counterpart
     * it remains an ordered compiler effect. Preserve it as the canonical
     * target-neutral compiler barrier rather than inventing an opaque
     * hardware instruction or dropping the volatile statement. */
    if (source->is_volatile && !source->is_goto &&
        source->template_text != NULL && source->template_length == 0U &&
        source->output_count == 0U && source->input_count == 0U &&
        source->label_count == 0U && source->clobber_count == 0U &&
        source->register_clobber_count == 0U &&
        !source->has_memory_clobber) {
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_COMPILER_BARRIER;
        instruction.span = statement->span;
        instruction.type = minic_type_void();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        *handled = true;
        return minic_core_function_append_effect_instruction(
            context->function, context->block_id, &instruction)
            ? MINIC_CORE_LOWER_OK : MINIC_CORE_LOWER_ERROR;
    }

    /* GNU's nonvolatile asm("") with no operands, labels or clobbers
     * contains no instructions, output values, or compiler barriers.
     * The original upstream Zstd uses this exact ordering-only source
     * form. Unlike a volatile or memory-clobber form there is no Core
     * effect to represent. Never discard any asm with a live operand. */
    if (!source->is_volatile && !source->is_goto &&
        source->template_text != NULL && source->template_length == 0U &&
        source->output_count == 0U && source->input_count == 0U &&
        source->label_count == 0U && source->clobber_count == 0U &&
        source->register_clobber_count == 0U &&
        !source->has_memory_clobber) {
        *handled = true;
        return MINIC_CORE_LOWER_OK;
    }

    /* M77_EMPTY_TIED_ASM_COPY: an empty, nonvolatile GNU asm with one
       register output tied to input 0 carries no target instruction semantics.
       It preserves the input register bit-pattern in the output. Model that
       target-neutrally as scalar bitcast/copy plus the output store. */
    if (!source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length == 0U && source->outputs != NULL && source->inputs != NULL &&
        source->output_count == 1U && source->input_count == 1U && source->label_count == 0U &&
        source->clobber_count == 0U && source->register_clobber_count == 0U &&
        !source->has_memory_clobber) {
        const MinicInlineAsmOperand *input = &source->inputs[0];
        const MinicInlineAsmOperand *output = &source->outputs[0];
        const MinicExpression *input_expression;
        const MinicExpression *output_expression;
        MinicCoreInstruction store;
        MinicCoreLowerStatus status;
        MinicCoreValueId input_value;
        MinicCoreValueId output_address;
        MinicCoreValueId output_value;
        MinicType input_type;
        MinicType output_type;

        input_expression =
            minic_c0_program_expression(context->body->program, input->expression);
        output_expression =
            minic_c0_program_expression(context->body->program, output->expression);
        if (output->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY &&
            core_inline_asm_register_output_constraint(output) &&
            input->access == MINIC_INLINE_ASM_OPERAND_READ_ONLY &&
            core_inline_asm_constraint_is(input, "0") && output_expression != NULL &&
            output_expression->value_category == MINIC_VALUE_LVALUE &&
            !minic_type_is_const(output_expression->type) &&
            minic_type_unqualified(output_expression->type, &output_type) &&
            core_memory_scalar_type(output_type) && input_expression != NULL &&
            core_scalar_expression_value_type(context->body, input_expression, &input_type)) {
            status = lower_expression(context, input->expression, &input_value);
            if (status != MINIC_CORE_LOWER_OK) {
                { *handled = true; return status; }
            }
            status = append_scalar_bitcast(
                context, statement->span, output_type, input_value, &output_value);
            if (status != MINIC_CORE_LOWER_OK) {
                { *handled = true; return status; }
            }
            status = lower_address(context, output->expression, &output_address);
            if (status != MINIC_CORE_LOWER_OK) {
                { *handled = true; return status; }
            }
            (void)memset(&store, 0, sizeof(store));
            store.kind = MINIC_CORE_INSTRUCTION_STORE;
            store.span = statement->span;
            store.type = minic_type_void();
            store.result = MINIC_CORE_VALUE_INVALID;
            store.value.store.address = output_address;
            store.value.store.stored_value = output_value;
            store.value.store.is_volatile = minic_type_is_volatile(output_expression->type);
            { *handled = true; return minic_core_function_append_effect_instruction(
                       context->function, context->block_id, &store)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR; }
        }
    }

    /* ORIGINAL LoongArch Linux 6.6.143 uses a file-scope hard register
     * __my_cpu_offset asm("$r21") in empty volatile +r, inside every
     * this_cpu_ptr/numa_node_id access. The source has no machine opcode,
     * so it must not be mistaken for an arbitrary register clobber or
     * discarded with a fake C value. Preserve compiler sequencing while
     * the following real FIXED_REGISTER_READ still reads live $r21.
     * Source binding was validated by the target at declaration; recheck
     * target write ownership and C type here without naming any ISA. */
    if (!source->is_goto && source->template_text != NULL &&
        source->template_length == 0U && source->outputs != NULL &&
        source->output_count == 1U && source->input_count == 0U &&
        source->label_count == 0U && source->clobber_count == 0U &&
        source->register_clobber_count == 0U &&
        !source->has_memory_clobber &&
        core_inline_asm_constraint_is(&source->outputs[0], "+r") &&
        source->outputs[0].access == MINIC_INLINE_ASM_OPERAND_READ_WRITE) {
        const MinicExpression *e = minic_c0_program_expression(
            context->body->program, source->outputs[0].expression);
        if (e && e->kind == MINIC_EXPRESSION_FIXED_REGISTER &&
            e->value_category == MINIC_VALUE_LVALUE &&
            core_memory_scalar_type(e->type) &&
            !minic_type_is_const(e->type) && !minic_type_is_volatile(e->type)) {
            const MinicFixedRegisterBinding *binding =
                minic_c0_program_fixed_register_binding(
                    context->body->program,
                    e->value.fixed_register_binding_id);
            if (binding && minic_type_equal(binding->type, e->type) &&
                minic_target_info_fixed_register_supported(
                    context->target, binding->register_name,
                    binding->register_name_length) &&
                minic_target_info_fixed_register_write_supported(
                    context->target, binding->register_name,
                    binding->register_name_length)) {
                *handled = true;
                if (!source->is_volatile)
                    return MINIC_CORE_LOWER_OK;
                (void)memset(&instruction, 0, sizeof(instruction));
                instruction.kind = MINIC_CORE_INSTRUCTION_COMPILER_BARRIER;
                instruction.span = statement->span;
                instruction.type = minic_type_void();
                instruction.result = MINIC_CORE_VALUE_INVALID;
                return minic_core_function_append_effect_instruction(
                    context->function, context->block_id, &instruction)
                    ? MINIC_CORE_LOWER_OK : MINIC_CORE_LOWER_ERROR;
            }
        }
    }

    /* Original BPF fentry +r(arg) is a zero-byte read/write asm. Since no
     * target instruction executes, a nonvolatile scalar local is unchanged;
     * only ordinary local C lvalues without physical hardreg bindings can
     * use this target-neutral no-op. Reuse the established +rm rule. */
    if (!source->is_goto && source->template_text != NULL &&
        source->template_length == 0U && source->outputs != NULL && source->output_count == 1U &&
        source->input_count == 0U && source->label_count == 0U && source->clobber_count == 0U &&
        source->register_clobber_count == 0U && !source->has_memory_clobber) {
        const MinicInlineAsmOperand *output;
        const MinicExpression *output_expression;
        const MinicLocal *local;

        output = &source->outputs[0];
        output_expression = minic_c0_program_expression(context->body->program, output->expression);
        if (output->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE &&
            output->constraint_text != NULL &&
            ((output->constraint_length == 3U &&
              memcmp(output->constraint_text, "+rm", 3U) == 0) ||
             (output->constraint_length == 2U &&
              memcmp(output->constraint_text, "+r", 2U) == 0)) &&
            output_expression != NULL &&
            output_expression->kind == MINIC_EXPRESSION_LOCAL &&
            output_expression->value_category == MINIC_VALUE_LVALUE &&
            core_memory_scalar_type(output_expression->type) &&
            !minic_type_is_const(output_expression->type) &&
            !minic_type_is_volatile(output_expression->type)) {
            local =
                minic_c0_program_local(context->body->program, output_expression->value.local_id);
            if (local == NULL) {
                { *handled = true; return MINIC_CORE_LOWER_ERROR; }
            }
            if (!local->is_array &&
                /* GNU local register storage is valid for empty +r. The
                 * compiler must reject only explicit physical hard-register
                 * bindings here; no instruction can alter an ordinary C
                 * register variable in an empty template. Keep +rm's
                 * original storage restrictions unchanged. */
                (!local->is_register_storage ||
                 (core_inline_asm_constraint_is(output, "+r") &&
                  minic_c0_program_local_fixed_register_binding(
                      context->body->program, output_expression->value.local_id) == NULL)) &&
                minic_type_equal(local->type, output_expression->type) &&
                !minic_type_is_const(local->type) && !minic_type_is_volatile(local->type)) {
                /* A nonvolatile empty read/write has no physical machine
                 * mutation, but GNU volatile requires a sequencing effect.
                 * Preserve that explicitly; do not silently discard it or
                 * invent a register write in the ABI-specific emitter. */
                if (source->is_volatile) {
                    /* Empty +r cannot change the C value: no hardware
                     * instructions execute. Yet the volatile qualifier is
                     * observable to the optimizer. Use the existing common
                     * compiler-only ordering effect, also consumed by all
                     * five machine emitters. Treating it as a memory barrier
                     * is deliberately more conservative than GNU's lack
                     * of a memory clobber, not less. An opaque machine-ASM
                     * opcode would spuriously require ISA-specific emitters. */
                    (void)memset(&instruction, 0, sizeof(instruction));
                    instruction.kind = MINIC_CORE_INSTRUCTION_COMPILER_BARRIER;
                    instruction.span = statement->span;
                    instruction.type = minic_type_void();
                    instruction.result = MINIC_CORE_VALUE_INVALID;
                    { *handled = true; return minic_core_function_append_effect_instruction(
                        context->function, context->block_id, &instruction)
                        ? MINIC_CORE_LOWER_OK : MINIC_CORE_LOWER_ERROR; }
                }
                { *handled = true; return MINIC_CORE_LOWER_OK; }
            }
        }
    }

    /* All zero-text outputless GNU register-input barriers share one Core
     * lowering, regardless of input arity (1..8) or optional memory clobber.
     * Every input is an evaluated C value and no physical instruction is
     * needed. Preserve the compiler ordering effect; no operand value may be
     * dropped before evaluation, and physical ISA remains target-owned. */
    if (!source->is_goto && source->template_text != NULL &&
        source->template_length == 0U && source->output_count == 0U &&
        source->inputs != NULL && source->input_count >= 1U &&
        source->input_count <= 8U && source->label_count == 0U &&
        source->register_clobber_count == 0U &&
        source->clobber_count == (source->has_memory_clobber ? 1U : 0U)) {
        size_t input_index;
        bool valid = true;
        for (input_index = 0U; input_index < source->input_count; ++input_index) {
            const MinicInlineAsmOperand *operand = &source->inputs[input_index];
            const MinicExpression *expression =
                minic_c0_program_expression(context->body->program, operand->expression);
            MinicType type;
            if (operand->access != MINIC_INLINE_ASM_OPERAND_READ_ONLY ||
                (!core_inline_asm_constraint_is(operand, "r") &&
                 !core_inline_asm_constraint_is(operand, "rK")) ||
                expression == NULL ||
                !core_scalar_expression_value_type(context->body, expression, &type)) {
                valid = false;
                break;
            }
        }
        if (valid) {
            for (input_index = 0U; input_index < source->input_count; ++input_index) {
                const MinicInlineAsmOperand *operand = &source->inputs[input_index];
                const MinicExpression *expression =
                    minic_c0_program_expression(context->body->program, operand->expression);
                MinicCoreValueId input_value;
                MinicType input_type;
                MinicCoreLowerStatus status;
                if (!core_scalar_expression_value_type(context->body, expression, &input_type)) {
                    *handled = true;
                    return MINIC_CORE_LOWER_ERROR;
                }
                status = lower_expression(context, operand->expression, &input_value);
                if (status != MINIC_CORE_LOWER_OK) {
                    *handled = true;
                    return status;
                }
                if (input_value >= context->function->value_count ||
                    !minic_type_equal(
                        context->function->values[input_value].type, input_type)) {
                    *handled = true;
                    return MINIC_CORE_LOWER_ERROR;
                }
            }
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_COMPILER_BARRIER;
            instruction.span = statement->span;
            instruction.type = minic_type_void();
            instruction.result = MINIC_CORE_VALUE_INVALID;
            *handled = true;
            return minic_core_function_append_effect_instruction(
                context->function, context->block_id, &instruction)
                ? MINIC_CORE_LOWER_OK : MINIC_CORE_LOWER_ERROR;
        }
    }

    /* BATCH_X_TWO_SCALAR_OUTPUTLESS_ASM_OPTIONAL_MEMORY: outputless GNU asm is
       effectively volatile (Batch F).  The two-register-input structured form
       is valid both for ordering-sensitive asm carrying a memory clobber and
       for MMIO-style asm whose template itself performs the access.  Preserve
       the actual memory effect flag rather than requiring one to exist. */
    if (!source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->output_count == 0U && source->inputs != NULL &&
        source->input_count == 2U && source->label_count == 0U &&
        source->register_clobber_count == 0U &&
        source->clobber_count == (source->has_memory_clobber ? 1U : 0U)) {
        MinicCoreInstruction structured;
        char *numeric_template = NULL;
        size_t numeric_template_length = 0U;
        size_t input_index;
        bool supported_shape = true;

        for (input_index = 0U; input_index < 2U; ++input_index) {
            const MinicInlineAsmOperand *operand = &source->inputs[input_index];
            const MinicExpression *input_expression = minic_c0_program_expression(
                context->body->program, operand->expression);
            MinicType input_type;
            bool register_constraint;

            register_constraint =
                operand->constraint_text != NULL &&
                ((operand->constraint_length == 1U &&
                  memcmp(operand->constraint_text, "r", 1U) == 0) ||
                 (operand->constraint_length == 2U &&
                  memcmp(operand->constraint_text, "rK", 2U) == 0));
            if (operand->access != MINIC_INLINE_ASM_OPERAND_READ_ONLY ||
                !register_constraint || input_expression == NULL ||
                !core_scalar_expression_value_type(context->body, input_expression, &input_type)) {
                supported_shape = false;
                break;
            }
        }
        if (supported_shape &&
            core_inline_asm_numeric_template(
                source, &numeric_template, &numeric_template_length)) {
            bool added;

            added = numeric_template_length != 0U &&
                    minic_core_function_add_opaque_inline_asm(context->function,
                                                              numeric_template,
                                                              numeric_template_length,
                                                              true,
                                                              source->has_memory_clobber,
                                                              &inline_asm_id);
            free(numeric_template);
            if (!added) {
                { *handled = true; return MINIC_CORE_LOWER_ERROR; }
            }
            (void)memset(&structured, 0, sizeof(structured));
            structured.kind = MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM;
            structured.span = statement->span;
            structured.type = minic_type_void();
            structured.result = MINIC_CORE_VALUE_INVALID;
            structured.value.structured_inline_asm.inline_asm_id = inline_asm_id;
            structured.value.structured_inline_asm.operand_count = 2U;
            for (input_index = 0U; input_index < 2U; ++input_index) {
                MinicCoreStructuredInlineAsmOperand *binding =
                    &structured.value.structured_inline_asm.operands[input_index];
                MinicCoreLowerStatus status;

                binding->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
                binding->operand_index = input_index;
                status = lower_expression(context, source->inputs[input_index].expression,
                                          &binding->value);
                if (status != MINIC_CORE_LOWER_OK) {
                    { *handled = true; return status; }
                }
            }
            { *handled = true; return minic_core_function_append_effect_instruction(
                       context->function, context->block_id, &structured)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR; }
        }
        free(numeric_template);
    }

    if (!source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->output_count == 0U && source->inputs != NULL &&
        source->input_count == 1U && source->label_count == 0U &&
        source->register_clobber_count == 0U &&
        source->clobber_count == (source->has_memory_clobber ? 1U : 0U)) {
        const MinicInlineAsmOperand *input;
        const MinicExpression *input_expression;
        MinicCoreValueId input_value;
        MinicCoreLowerStatus input_status;
        MinicType input_type;
        bool register_constraint;

        input = &source->inputs[0];
        input_expression = minic_c0_program_expression(context->body->program, input->expression);
        register_constraint =
            input->constraint_text != NULL &&
            ((input->constraint_length == 1U &&
              memcmp(input->constraint_text, "r", 1U) == 0) ||
             (input->constraint_length == 2U &&
              memcmp(input->constraint_text, "rK", 2U) == 0));
        if (input->access == MINIC_INLINE_ASM_OPERAND_READ_ONLY && register_constraint &&
            input_expression != NULL &&
            core_scalar_expression_value_type(context->body, input_expression, &input_type)) {
            input_status = lower_expression(context, input->expression, &input_value);
            if (input_status != MINIC_CORE_LOWER_OK) {
                { *handled = true; return input_status; }
            }
            if (input_value >= context->function->value_count ||
                !minic_type_equal(context->function->values[input_value].type, input_type)) {
                { *handled = true; return MINIC_CORE_LOWER_ERROR; }
            }
            if (!minic_core_function_add_opaque_inline_asm(context->function,
                                                           source->template_text,
                                                           source->template_length,
                                                           true,
                                                           source->has_memory_clobber,
                                                           &inline_asm_id)) {
                { *handled = true; return MINIC_CORE_LOWER_ERROR; }
            }
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_SCALAR_INPUT_INLINE_ASM;
            instruction.span = statement->span;
            instruction.type = minic_type_void();
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.scalar_input_inline_asm.inline_asm_id = inline_asm_id;
            instruction.value.scalar_input_inline_asm.operand = input_value;
            { *handled = true; return minic_core_function_append_effect_instruction(
                       context->function, context->block_id, &instruction)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR; }
        }
    }

    return MINIC_CORE_LOWER_OK;
}

