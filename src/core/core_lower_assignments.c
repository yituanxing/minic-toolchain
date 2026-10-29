#include "core/core_lower_internal.h"
#include "core/local_facts.h"
#include "core/cfg_constant_calls.h"
#include "frontend/const_eval.h"
#include "frontend/expression_semantics.h"
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* One semantic family: operand conversion/snapshot, lvalue evaluation,
 * effectful storage, local facts, and scalar prefix/postfix updates.
 * Bit-field assignments dispatch to the dedicated semantic owner. */

static MinicCoreLowerStatus minic_core_lower_assignment_pair_impl(MinicCoreLowerContext *context,
                                                  MinicExpressionId target_id,
                                                  MinicExpressionId source_id,
                                                  MinicSourceSpan span,
                                                  MinicCoreValueId *result_value,
                                                  bool is_initializer_write) {
    const MinicExpression *target;
    const MinicExpression *source;
    const MinicExpression *source_operand;
    MinicCoreObjectId stored_object;
    MinicCoreValueId address_id;
    MinicCoreValueId stored_value;
    MinicCoreLowerStatus status;
    MinicType stored_type;
    int source_kind;
    int source_operand_kind;

    if (context == NULL || context->body == NULL || context->body->program == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    target = minic_c0_program_expression(context->body->program, target_id);
    source = minic_c0_program_expression(context->body->program, source_id);
    source_operand = NULL;
    source_kind = source != NULL ? (int)source->kind : -1;
    source_operand_kind = -1;
    if (source != NULL && source->kind == MINIC_EXPRESSION_ADDRESS_OF) {
        source_operand = minic_c0_program_expression(
            context->body->program, source->value.unary.operand);
        if (source_operand != NULL) {
            source_operand_kind = (int)source_operand->kind;
        }
    }
    if (target == NULL || target->value_category != MINIC_VALUE_LVALUE) {
        return MINIC_CORE_LOWER_ERROR;
    }
    if (target->kind == MINIC_EXPRESSION_FIXED_REGISTER) {
        const MinicFixedRegisterBinding *binding;
        MinicCoreInstruction effect;
        size_t core_binding_id;
        binding = minic_c0_program_fixed_register_binding(
            context->body->program, target->value.fixed_register_binding_id);
        if (binding == NULL || binding->is_local ||
            !minic_target_info_fixed_register_write_supported(
                context->target, binding->register_name, binding->register_name_length) ||
            !minic_type_unqualified(target->type, &stored_type) ||
            !core_memory_scalar_type(stored_type) ||
            !minic_type_equal(binding->type, stored_type))
            return MINIC_CORE_LOWER_UNSUPPORTED;
        status = minic_core_lower_internal_lower_scalar_assignment_value(
            context, stored_type, source_id, &stored_value);
        if (status != MINIC_CORE_LOWER_OK)
            return status;
        if (!core_import_fixed_register_binding(
                context, target->value.fixed_register_binding_id, &core_binding_id))
            return MINIC_CORE_LOWER_ERROR;
        (void)memset(&effect, 0, sizeof(effect));
        effect.kind = MINIC_CORE_INSTRUCTION_FIXED_REGISTER_WRITE;
        effect.span = span;
        effect.type = minic_type_void();
        effect.result = MINIC_CORE_VALUE_INVALID;
        effect.value.fixed_register_write.binding_id = core_binding_id;
        effect.value.fixed_register_write.source = stored_value;
        if (!minic_core_function_append_effect_instruction(
                context->function, context->block_id, &effect))
            return MINIC_CORE_LOWER_ERROR;
        if (result_value != NULL) *result_value = stored_value;
        return MINIC_CORE_LOWER_OK;
    }
    {
        bool matched;
        status = minic_core_lower_try_bit_field_assignment(
            context, target, source_id, span, result_value, is_initializer_write, &matched);
        if (matched || status != MINIC_CORE_LOWER_OK) return status;
    }
    if (!minic_type_unqualified(target->type, &stored_type) ||
        !core_memory_scalar_type(stored_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    status = minic_core_lower_internal_lower_scalar_assignment_value(context, stored_type, source_id, &stored_value);
    if (status != MINIC_CORE_LOWER_OK) {
        {
            const MinicExpression *target_expression;
            const MinicExpression *source_expression;

            target_expression =
                minic_c0_program_expression(context->body->program, target_id);
            source_expression =
                minic_c0_program_expression(context->body->program, source_id);
            (void)fprintf(stderr,
                          "CORE_ASSIGN_STAGE function=%s stage=value status=%d "
                          "source_kind=%d operand_kind=%d target_base=%d target_ptr=%u "
                          "source_base=%d source_ptr=%u\n",
                          context->source_function != NULL ? context->source_function->name : "?",
                          (int)status,
                          source_kind,
                          source_operand_kind,
                          target_expression != NULL ? (int)target_expression->type.base_kind : -1,
                          target_expression != NULL ? target_expression->type.pointer_depth : 0U,
                          source_expression != NULL ? (int)source_expression->type.base_kind : -1,
                          source_expression != NULL ? source_expression->type.pointer_depth : 0U);
        }
        return status;
    }
    status = minic_core_lower_internal_spill_scalar_value(context, span, stored_type, stored_value, &stored_object);
    if (status != MINIC_CORE_LOWER_OK) {
        (void)fprintf(stderr, "CORE_ASSIGN_STAGE function=%s stage=spill status=%d\n",
                      context->source_function != NULL ? context->source_function->name : "?",
                      (int)status);
        return status;
    }
    status = lower_address(context, target_id, &address_id);
    if (status != MINIC_CORE_LOWER_OK) {
        (void)fprintf(stderr, "CORE_ASSIGN_STAGE function=%s stage=target-address status=%d\n",
                      context->source_function != NULL ? context->source_function->name : "?",
                      (int)status);
        return status;
    }
    status = minic_core_lower_internal_reload_scalar_value(context, span, stored_type, stored_object, &stored_value);
    if (status != MINIC_CORE_LOWER_OK) {
        (void)fprintf(stderr, "CORE_ASSIGN_STAGE function=%s stage=reload status=%d\n",
                      context->source_function != NULL ? context->source_function->name : "?",
                      (int)status);
        return status;
    }
    if (minic_core_lower_internal_emit_scalar_store(
            context, span, address_id, stored_value,
            minic_type_is_volatile(target->type)) != MINIC_CORE_LOWER_OK) {
        (void)fprintf(stderr, "CORE_ASSIGN_STAGE function=%s stage=store status=%d\n",
                      context->source_function != NULL ? context->source_function->name : "?",
                      (int)MINIC_CORE_LOWER_ERROR);
        return MINIC_CORE_LOWER_ERROR;
    }
    if (target->kind == MINIC_EXPRESSION_LOCAL) {
        MinicConstValue stored_constant;
        bool fact_known;
        bool null_pointer_known;
        null_pointer_known = minic_core_lower_known_null_pointer(context, source_id);
        /* M191_STATEMENT_EXPRESSION_POSTLOWER_FACT: minic_core_lower_internal_lower_scalar_assignment_value()
           has already executed a GNU statement-expression block at this point.
           If its final expression is now a proven integer local fact, consume
           that fact directly instead of asking the intentionally narrow
           pre-lowering statement-expression interpreter to replay control flow. */
        fact_known = false;
        if (source != NULL && source->kind == MINIC_EXPRESSION_STATEMENT &&
            source->value.statement_expression.result != MINIC_EXPRESSION_INVALID) {
            MinicConstValue statement_result_constant;
            MinicConstValue converted_statement_result;
            if (core_const_eval_integer_with_locals(
                    context,
                    source->value.statement_expression.result,
                    &statement_result_constant) &&
                minic_const_value_convert_integer(context->body->program,
                                                  context->target,
                                                  &statement_result_constant,
                                                  stored_type,
                                                  &converted_statement_result)) {
                stored_constant = converted_statement_result;
                fact_known = true;
            }
        }
        minic_core_local_fact_invalidate(context, target->value.local_id);
        if (!fact_known) {
            fact_known = core_const_eval_integer_with_locals(
                context, source_id, &stored_constant);
        }
        if (fact_known) {
            core_local_constant_set(context, target->value.local_id, &stored_constant);
        } else if (null_pointer_known) {
            minic_core_local_null_pointer_set(context, target->value.local_id);
        }
        if (getenv("MINIC_LOCAL_FACT_TRACE") != NULL) {
            (void)fprintf(stderr,
                          "LOCAL_FACT_ASSIGN function=%s target=%zu source=%zu source_kind=%d known=%d null=%d bits=%" PRIu64 "\n",
                          context->source_function != NULL ? context->source_function->name : "?",
                          (size_t)target->value.local_id,
                          (size_t)source_id,
                          source != NULL ? (int)source->kind : -1,
                          fact_known ? 1 : 0,
                          null_pointer_known ? 1 : 0,
                          fact_known ? stored_constant.bits : UINT64_C(0));
        }
    }
    if (result_value != NULL) {
        *result_value = stored_value;
    }
    return MINIC_CORE_LOWER_OK;
}

MinicCoreLowerStatus minic_core_lower_assignment_pair(MinicCoreLowerContext *context,
                                                  MinicExpressionId target_id,
                                                  MinicExpressionId source_id,
                                                  MinicSourceSpan span,
                                                  MinicCoreValueId *result_value) {
    return minic_core_lower_assignment_pair_impl(context, target_id, source_id,
                                                 span, result_value, false);
}

MinicCoreLowerStatus minic_core_lower_assignment(MinicCoreLowerContext *context,
                                             const MinicStatement *statement) {
    MinicExpressionId source_id;
    MinicExpressionId target_id;

    if (statement == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    target_id = statement->target_expression;
    source_id = statement->expression;
    return minic_core_lower_assignment_pair_impl(context, target_id, source_id,
                                                 statement->span, NULL,
                                                 statement->is_initializer_write);
}

/* M102_UNSIGNED_BIT_FIELD_SCALAR_UPDATE / M154_SIGNED_BIT_FIELD_UPDATE_OWNER:
   prefix/postfix ++/-- on an integer bit-field cannot use the ordinary
   addressable-lvalue update path.  Keep the allocation-unit RMW unsigned,
   but reconstruct signed field values from their declared width before
   promotion and after truncating the updated value. */
MinicCoreLowerStatus minic_core_lower_scalar_update(MinicCoreLowerContext *context,
                                                const MinicExpression *expression,
                                                MinicCoreValueId *value_id) {
    const MinicExpression *operand;
    MinicCoreInstruction instruction;
    MinicCoreValueId address;
    MinicCoreValueId current;
    MinicCoreValueId delta;
    MinicCoreValueId one;
    MinicCoreValueId updated;
    MinicCoreLowerStatus status;
    MinicType expression_value_type;
    MinicType stored_type;
    bool increment;
    bool prefix;

    /* M56_PREFIX_POSTFIX_SCALAR_UPDATE: both forms perform the same single
       load/update/store. Only the expression result differs: prefix yields the
       updated value, postfix yields the prior value. */
    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || expression == NULL || value_id == NULL ||
        expression->kind != MINIC_EXPRESSION_UNARY ||
        (expression->value.unary.operator_kind != MINIC_UNARY_POST_INCREMENT &&
         expression->value.unary.operator_kind != MINIC_UNARY_POST_DECREMENT &&
         expression->value.unary.operator_kind != MINIC_UNARY_PRE_INCREMENT &&
         expression->value.unary.operator_kind != MINIC_UNARY_PRE_DECREMENT)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    increment = expression->value.unary.operator_kind == MINIC_UNARY_POST_INCREMENT ||
                expression->value.unary.operator_kind == MINIC_UNARY_PRE_INCREMENT;
    prefix = expression->value.unary.operator_kind == MINIC_UNARY_PRE_INCREMENT ||
             expression->value.unary.operator_kind == MINIC_UNARY_PRE_DECREMENT;
    operand = minic_c0_program_expression(context->body->program, expression->value.unary.operand);
    /* M63_QUALIFIED_SCALAR_UPDATE_VALUE: the memory operand may be qualified
       (notably volatile), while the computed prefix/postfix value transported
       by Core is an ordinary unqualified scalar. Preserve qualifiers solely on
       the load/store effects and compare the expression's value type after
       unqualification. */
    if (operand == NULL || operand->value_category != MINIC_VALUE_LVALUE ||
        !core_memory_scalar_type(operand->type) || minic_type_is_const(operand->type) ||
        !minic_type_unqualified(operand->type, &stored_type) ||
        !minic_type_unqualified(expression->type, &expression_value_type) ||
        !minic_type_equal(expression_value_type, stored_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    if (minic_type_is_integer(stored_type) && minic_type_is_bool_integer(stored_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    if (operand->kind == MINIC_EXPRESSION_MEMBER) {
        const MinicRecord *update_record =
            minic_c0_program_record(context->body->program, operand->value.member.record_id);
        const MinicRecordField *update_field =
            minic_c0_record_field(update_record, operand->value.member.field_index);

        if (update_field != NULL && update_field->is_bit_field) {
            return minic_core_lower_bit_field_update(
                context, expression, operand, increment, prefix, value_id);
        }
    }
    status = lower_address(context, expression->value.unary.operand, &address);
    if (status != MINIC_CORE_LOWER_OK) {
        if (getenv("MINIC_CORE_EXPR_FAILURE_TRACE") != NULL)
            (void)fprintf(stderr,
                "CORE_EXPR_FAIL owner=update-address function=%s status=%d "
                "operand_kind=%d unary_op=%d block=%u\n",
                context->source_function != NULL ? context->source_function->name : "?",
                (int)status, operand == NULL ? -1 : (int)operand->kind,
                (int)expression->value.unary.operator_kind, (unsigned)context->block_id);
        return status;
    }

    status = minic_core_lower_internal_emit_scalar_load(
        context, expression->span, stored_type, address,
        minic_type_is_volatile(operand->type), &current);
    if (status != MINIC_CORE_LOWER_OK) return status;

    if (minic_type_is_integer(stored_type)) {
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
        instruction.span = expression->span;
        instruction.type = stored_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.integer_value = 1;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &one)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        delta = one;
        if (!increment) {
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_NEGATE;
            instruction.span = expression->span;
            instruction.type = stored_type;
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.operand = one;
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &instruction, &delta)) {
                return MINIC_CORE_LOWER_ERROR;
            }
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_ADD;
        instruction.span = expression->span;
        instruction.type = stored_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = current;
        instruction.value.binary.right = delta;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &updated)) {
            return MINIC_CORE_LOWER_ERROR;
        }
    } else if (minic_type_is_float(stored_type) || minic_type_is_double(stored_type)) {
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT;
        instruction.span = expression->span;
        instruction.type = stored_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.floating_bits =
            minic_type_is_float(stored_type)
                ? UINT64_C(0x3f800000)
                : UINT64_C(0x3ff0000000000000);
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &one)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind =
            minic_type_is_float(stored_type)
                ? (increment ? MINIC_CORE_INSTRUCTION_FLOAT_ADD
                             : MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT)
                : (increment ? MINIC_CORE_INSTRUCTION_DOUBLE_ADD
                             : MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT);
        instruction.span = expression->span;
        instruction.type = stored_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = current;
        instruction.value.binary.right = one;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &updated)) {
            return MINIC_CORE_LOWER_ERROR;
        }
    } else if (minic_type_is_long_double(stored_type)) {
        const char *helper = increment ? "__addtf3" : "__subtf3";

        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT;
        instruction.span = expression->span;
        instruction.type = stored_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.floating128_bits.low = UINT64_C(0);
        instruction.value.floating128_bits.high = UINT64_C(0x3fff000000000000);
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &one)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = minic_core_lower_internal_append_long_double_binary_helper(
            context, expression->span, helper, current, one, stored_type, &updated);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
    } else if (minic_type_is_pointer(stored_type)) {
        size_t element_size;

        if (!minic_c0_pointer_arithmetic_element_size(
                context->body->program, core_data_layout(context), stored_type, &element_size)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
        instruction.span = expression->span;
        instruction.type = minic_type_int();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.integer_value = increment ? 1 : -1;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &delta)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_POINTER_OFFSET;
        instruction.span = expression->span;
        instruction.type = stored_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.pointer_offset.base = current;
        instruction.value.pointer_offset.index = delta;
        instruction.value.pointer_offset.element_size = element_size;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &updated)) {
            return MINIC_CORE_LOWER_ERROR;
        }
    } else {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }

    status = minic_core_lower_internal_emit_scalar_store(
        context, expression->span, address, updated,
        minic_type_is_volatile(operand->type));
    if (status != MINIC_CORE_LOWER_OK) return status;
    *value_id = prefix ? updated : current;
    return MINIC_CORE_LOWER_OK;
}

/* Pointer compound RMW is owned by assignment semantics. Address and
 * original pointer are snapshotted before any RHS expression that can split
 * Core CFG; a failed admission leaves integer compound handling unclaimed. */
MinicCoreLowerStatus minic_core_lower_try_pointer_compound_assignment(
    MinicCoreLowerContext *context, const MinicExpression *expression,
    MinicCoreValueId *value_id, bool *handled) {
    MinicCoreInstruction instruction;
    if (handled == NULL || context == NULL || expression == NULL || value_id == NULL ||
        context->body == NULL || context->body->program == NULL ||
        context->function == NULL)
        return MINIC_CORE_LOWER_ERROR;
    *handled = false;
    /* M75_POINTER_COMPOUND_ASSIGNMENT_VALUE: pointer += / -= evaluates
       the destination lvalue once, loads its current pointer value, applies a
       scaled integer offset, stores the updated pointer, and yields that value.
       Keep subtraction explicit in Core: negating an unsigned RHS before
       pointer.offset would change its width/extension semantics. */
    if (expression->kind == MINIC_EXPRESSION_COMPOUND_ASSIGNMENT) {
        const MinicExpression *source;
        const MinicExpression *target;
        MinicCoreObjectId address_object;
        MinicCoreObjectId current_object;
        MinicCoreValueId address;
        MinicCoreValueId current;
        MinicCoreValueId index;
        MinicCoreValueId updated;
        MinicCoreLowerStatus status;
        MinicType address_type;
        MinicType expression_value_type;
        MinicType index_type;
        MinicType stored_type;
        size_t element_size;
        bool subtract;

        target = minic_c0_program_expression(
            context->body->program, expression->value.binary.left);
        if (target == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        /* M76_POINTER_COMPOUND_DISPATCH: only claim compound assignments whose
           destination is actually a pointer. Integer +=/-=/&=/|=/... must
           continue to the established M51 integer compound-assignment path. */
        if (minic_type_unqualified(target->type, &stored_type) &&
            minic_type_is_pointer(stored_type)) {
            *handled = true;
            source = minic_c0_program_expression(
                context->body->program, expression->value.binary.right);
            subtract = expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT;
            if (source == NULL || target->value_category != MINIC_VALUE_LVALUE ||
                minic_type_is_const(target->type) ||
                (expression->value.binary.operator_kind != MINIC_BINARY_ADD &&
                 expression->value.binary.operator_kind != MINIC_BINARY_SUBTRACT) ||
                !minic_type_unqualified(expression->type, &expression_value_type) ||
                !minic_type_equal(expression_value_type, stored_type) ||
                !core_scalar_expression_value_type(context->body, source, &index_type) ||
                !minic_type_is_integer(index_type) ||
                !minic_c0_pointer_arithmetic_element_size(context->body->program,
                                                          core_data_layout(context),
                                                          stored_type,
                                                          &element_size)) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            status = lower_address(context, expression->value.binary.left, &address);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            status = minic_core_lower_internal_emit_scalar_load(
                context, expression->span, stored_type, address,
                minic_type_is_volatile(target->type), &current);
            if (status != MINIC_CORE_LOWER_OK) return status;
            /* M126A_POINTER_COMPOUND_BLOCK_LOCAL: the RHS may create CFG (for
               example a call with a conditional argument). Preserve both the
               destination address and its pre-update pointer value before
               lowering that RHS so POINTER_OFFSET and STORE are formed from
               values reloaded in the final RHS block. */
            if (address >= context->function->value_count) {
                return MINIC_CORE_LOWER_ERROR;
            }
            address_type = context->function->values[address].type;
            status = minic_core_lower_internal_spill_scalar_value(
                context, target->span, address_type, address, &address_object);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            status = minic_core_lower_internal_spill_scalar_value(
                context, target->span, stored_type, current, &current_object);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            status = lower_expression(context, expression->value.binary.right, &index);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            if (index >= context->function->value_count ||
                !minic_type_equal(context->function->values[index].type, index_type)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            status = minic_core_lower_internal_reload_scalar_value(
                context, target->span, stored_type, current_object, &current);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            status = minic_core_lower_internal_reload_scalar_value(
                context, target->span, address_type, address_object, &address);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_POINTER_OFFSET;
            instruction.span = expression->span;
            instruction.type = stored_type;
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.pointer_offset.base = current;
            instruction.value.pointer_offset.index = index;
            instruction.value.pointer_offset.element_size = element_size;
            instruction.value.pointer_offset.subtract = subtract;
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &instruction, &updated)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            status = minic_core_lower_internal_emit_scalar_store(
                context, expression->span, address, updated,
                minic_type_is_volatile(target->type));
            if (status != MINIC_CORE_LOWER_OK) return status;
            *value_id = updated;
            return MINIC_CORE_LOWER_OK;
        }
    }

    return MINIC_CORE_LOWER_OK;
}

