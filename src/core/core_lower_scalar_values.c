#include "core/core_lower_internal.h"
#include "frontend/const_eval.h"
#include "frontend/expression_semantics.h"
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Core scalar values own casts, bitcasts, value snapshots, scalar helpers. */

MinicCoreLowerStatus minic_core_lower_internal_append_integer_conversion(MinicCoreLowerContext *context,
                                                      MinicSourceSpan span,
                                                      MinicType target_type,
                                                      MinicCoreValueId source_value,
                                                      MinicCoreValueId *value_id) {
    MinicCoreInstruction instruction;
    const MinicCoreValue *source;

    if (context == NULL || context->function == NULL || value_id == NULL ||
        source_value >= context->function->value_count || !minic_type_is_integer(target_type)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    source = &context->function->values[source_value];
    if (!minic_type_is_integer(source->type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    if (minic_type_equal(source->type, target_type)) {
        *value_id = source_value;
        return MINIC_CORE_LOWER_OK;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION;
    instruction.span = span;
    instruction.type = target_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.operand = source_value;
    return minic_core_function_append_value_instruction(
               context->function, context->block_id, &instruction, value_id)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

MinicCoreLowerStatus append_scalar_bitcast(MinicCoreLowerContext *context,
                                                  MinicSourceSpan span,
                                                  MinicType target_type,
                                                  MinicCoreValueId source_value,
                                                  MinicCoreValueId *value_id) {
    MinicCoreInstruction instruction;
    const MinicCoreValue *source;

    if (context == NULL || context->function == NULL || value_id == NULL ||
        source_value >= context->function->value_count) {
        return MINIC_CORE_LOWER_ERROR;
    }
    source = &context->function->values[source_value];
    if (minic_type_equal(source->type, target_type)) {
        *value_id = source_value;
        return MINIC_CORE_LOWER_OK;
    }
    if (!minic_core_scalar_bitcast_types_valid(target_type, source->type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_SCALAR_BITCAST;
    instruction.span = span;
    instruction.type = target_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.operand = source_value;
    return minic_core_function_append_value_instruction(
               context->function, context->block_id, &instruction, value_id)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}


/* Only Core scalar-values owns machine-independent memory-effect building. */
MinicCoreLowerStatus minic_core_lower_internal_emit_scalar_load(
    MinicCoreLowerContext *context, MinicSourceSpan span, MinicType type,
    MinicCoreValueId address_id, bool is_volatile, MinicCoreValueId *value_id) {
    MinicCoreInstruction instruction;
    if (context == NULL || context->function == NULL || value_id == NULL)
        return MINIC_CORE_LOWER_ERROR;
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_LOAD;
    instruction.span = span;
    instruction.type = type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.load.address = address_id;
    instruction.value.load.is_volatile = is_volatile;
    return minic_core_function_append_value_instruction(
               context->function, context->block_id, &instruction, value_id)
               ? MINIC_CORE_LOWER_OK : MINIC_CORE_LOWER_ERROR;
}

MinicCoreLowerStatus minic_core_lower_internal_emit_scalar_store(
    MinicCoreLowerContext *context, MinicSourceSpan span,
    MinicCoreValueId address_id, MinicCoreValueId stored_value, bool is_volatile) {
    MinicCoreInstruction instruction;
    if (context == NULL || context->function == NULL)
        return MINIC_CORE_LOWER_ERROR;
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
    instruction.span = span;
    instruction.type = minic_type_void();
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.store.address = address_id;
    instruction.value.store.stored_value = stored_value;
    instruction.value.store.is_volatile = is_volatile;
    return minic_core_function_append_effect_instruction(
               context->function, context->block_id, &instruction)
               ? MINIC_CORE_LOWER_OK : MINIC_CORE_LOWER_ERROR;
}

static MinicCoreLowerStatus core_scalar_object_address(
    MinicCoreLowerContext *context, MinicSourceSpan span, MinicType pointer_type,
    MinicCoreObjectId object_id, MinicCoreValueId *address_id) {
    MinicCoreInstruction instruction;
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
    instruction.span = span;
    instruction.type = pointer_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.object_id = object_id;
    return minic_core_function_append_value_instruction(
               context->function, context->block_id, &instruction, address_id)
               ? MINIC_CORE_LOWER_OK : MINIC_CORE_LOWER_ERROR;
}

MinicCoreLowerStatus minic_core_lower_internal_spill_scalar_value(MinicCoreLowerContext *context,
                                               MinicSourceSpan span,
                                               MinicType type,
                                               MinicCoreValueId value_id,
                                               MinicCoreObjectId *object_id) {
    MinicCoreValueId address_id;
    MinicType pointer_type;

    if (context == NULL || context->function == NULL || object_id == NULL ||
        !core_memory_scalar_type(type) || minic_type_is_const(type) ||
        minic_type_is_volatile(type) || value_id >= context->function->value_count ||
        !minic_type_equal(context->function->values[value_id].type, type)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    if (!minic_core_function_add_object(context->function, span, type, object_id) ||
        !minic_type_pointer_to(type, &pointer_type)) {
        return MINIC_CORE_LOWER_ERROR;
    }

    if (core_scalar_object_address(context, span, pointer_type,
                                   *object_id, &address_id) != MINIC_CORE_LOWER_OK)
        return MINIC_CORE_LOWER_ERROR;

    return minic_core_lower_internal_emit_scalar_store(
        context, span, address_id, value_id, false);
}

MinicCoreLowerStatus minic_core_lower_internal_store_scalar_value(MinicCoreLowerContext *context,
                                               MinicSourceSpan span,
                                               MinicType type,
                                               MinicCoreObjectId object_id,
                                               MinicCoreValueId value_id) {
    MinicCoreValueId address_id;
    MinicType pointer_type;

    if (context == NULL || context->function == NULL || !core_memory_scalar_type(type) ||
        minic_type_is_const(type) || minic_type_is_volatile(type) ||
        object_id >= context->function->object_count ||
        value_id >= context->function->value_count ||
        !minic_type_equal(context->function->objects[object_id].type, type) ||
        !minic_type_equal(context->function->values[value_id].type, type) ||
        !minic_type_pointer_to(type, &pointer_type)) {
        return MINIC_CORE_LOWER_ERROR;
    }

    if (core_scalar_object_address(context, span, pointer_type,
                                   object_id, &address_id) != MINIC_CORE_LOWER_OK)
        return MINIC_CORE_LOWER_ERROR;

    return minic_core_lower_internal_emit_scalar_store(
        context, span, address_id, value_id, false);
}

MinicCoreLowerStatus minic_core_lower_internal_reload_scalar_value(MinicCoreLowerContext *context,
                                                MinicSourceSpan span,
                                                MinicType type,
                                                MinicCoreObjectId object_id,
                                                MinicCoreValueId *value_id) {
    MinicCoreValueId address_id;
    MinicType pointer_type;

    if (context == NULL || context->function == NULL || value_id == NULL ||
        !core_memory_scalar_type(type) || minic_type_is_const(type) ||
        minic_type_is_volatile(type) || object_id >= context->function->object_count ||
        !minic_type_equal(context->function->objects[object_id].type, type) ||
        !minic_type_pointer_to(type, &pointer_type)) {
        return MINIC_CORE_LOWER_ERROR;
    }

    if (core_scalar_object_address(context, span, pointer_type,
                                   object_id, &address_id) != MINIC_CORE_LOWER_OK)
        return MINIC_CORE_LOWER_ERROR;

    return minic_core_lower_internal_emit_scalar_load(
        context, span, type, address_id, false, value_id);
}

MinicCoreLowerStatus minic_core_lower_internal_lower_scalar_equality_operands(MinicCoreLowerContext *context,
                                                           MinicExpressionId left_id,
                                                           MinicExpressionId right_id,
                                                           MinicCoreValueId *left_value,
                                                           MinicCoreValueId *right_value) {
    const MinicExpression *left_expression;
    const MinicExpression *right_expression;
    MinicCoreObjectId left_object;
    MinicCoreValueId left_normalized;
    MinicCoreValueId left_source;
    MinicCoreValueId right_normalized;
    MinicCoreValueId right_source;
    MinicCoreLowerStatus status;
    MinicType comparison_type;
    MinicType left_type;
    MinicType right_type;
    bool integer_comparison;
    bool pointer_comparison;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || left_value == NULL || right_value == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    left_expression = minic_c0_program_expression(context->body->program, left_id);
    right_expression = minic_c0_program_expression(context->body->program, right_id);
    if (left_expression == NULL || right_expression == NULL ||
        !core_scalar_expression_value_type(context->body, left_expression, &left_type) ||
        !core_scalar_expression_value_type(context->body, right_expression, &right_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }

    integer_comparison = false;
    pointer_comparison = false;
    if (minic_type_is_integer(left_type) && minic_type_is_integer(right_type)) {
        if (context->target == NULL ||
            !minic_target_info_integer_common_for_program(
                context->target, context->body->program, left_type, right_type, &comparison_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        integer_comparison = true;
    } else if (minic_type_is_pointer(left_type) && minic_type_is_pointer(right_type)) {
        /* BATCH_T_FRONTEND_OWNED_POINTER_EQUALITY: legality belongs to the
           source-language semantic layer.  In particular GNU C accepts the
           established function-pointer <-> void-pointer equality extension.
           Once frontend semantics accept the expression, Core only needs one
           common pointer representation for SCALAR_EQUAL.  Prefer the normal
           C conditional common pointer type; when the GNU extension has no C
           common type, use the left representation and bitcast both operands. */
        if (!minic_c0_pointer_equality_compatible(
                context->body->program, left_id, right_id)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        if (!minic_type_conditional_pointer_common(
                left_type, right_type, &comparison_type)) {
            comparison_type = left_type;
        }
        pointer_comparison = true;
    } else if (minic_type_is_pointer(left_type) && minic_type_is_integer(right_type) &&
               minic_c0_expression_is_null_pointer_constant_v0(context->body->program, right_id)) {
        comparison_type = left_type;
        pointer_comparison = true;
    } else if (minic_type_is_integer(left_type) && minic_type_is_pointer(right_type) &&
               minic_c0_expression_is_null_pointer_constant_v0(context->body->program, left_id)) {
        comparison_type = right_type;
        pointer_comparison = true;
    } else {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }

    status = lower_expression(context, left_id, &left_source);
    if (status != MINIC_CORE_LOWER_OK) {
        if (getenv("MINIC_CORE_EXPR_FAILURE_TRACE") != NULL)
            (void)fprintf(stderr,
                "CORE_EXPR_FAIL owner=equality-left-lower function=%s status=%d "
                "left_kind=%d op=%d id=%zu block=%u\n",
                context->source_function != NULL ? context->source_function->name : "?",
                (int)status, (int)left_expression->kind,
                left_expression->kind == MINIC_EXPRESSION_UNARY
                    ? (int)left_expression->value.unary.operator_kind : -1,
                (size_t)left_id, (unsigned)context->block_id);
        return status;
    }
    if (left_source >= context->function->value_count ||
        !minic_type_equal(context->function->values[left_source].type, left_type)) {
        if (getenv("MINIC_CORE_EXPR_FAILURE_TRACE") != NULL)
            (void)fprintf(stderr,
                "CORE_EXPR_FAIL owner=equality-left-type function=%s id=%zu "
                "value=%u wanted_kind=%d actual_kind=%d\n",
                context->source_function != NULL ? context->source_function->name : "?",
                (size_t)left_id, (unsigned)left_source,
                (int)left_type.base_kind,
                left_source < context->function->value_count
                    ? (int)context->function->values[left_source].type.base_kind : -1);
        return MINIC_CORE_LOWER_ERROR;
    }
    if (integer_comparison) {
        status = minic_core_lower_internal_append_integer_conversion(
            context, left_expression->span, comparison_type, left_source, &left_normalized);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
    } else if (pointer_comparison) {
        status = append_scalar_bitcast(
            context, left_expression->span, comparison_type, left_source, &left_normalized);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
    } else {
        return MINIC_CORE_LOWER_ERROR;
    }
    status = minic_core_lower_internal_spill_scalar_value(
        context, left_expression->span, comparison_type, left_normalized, &left_object);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }

    status = lower_expression(context, right_id, &right_source);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    if (right_source >= context->function->value_count ||
        !minic_type_equal(context->function->values[right_source].type, right_type)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    if (integer_comparison) {
        status = minic_core_lower_internal_append_integer_conversion(
            context, right_expression->span, comparison_type, right_source, &right_normalized);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
    } else if (pointer_comparison) {
        status = append_scalar_bitcast(
            context, right_expression->span, comparison_type, right_source, &right_normalized);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
    } else {
        return MINIC_CORE_LOWER_ERROR;
    }
    status = minic_core_lower_internal_reload_scalar_value(
        context, left_expression->span, comparison_type, left_object, left_value);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    *right_value = right_normalized;
    return MINIC_CORE_LOWER_OK;
}

MinicCoreLowerStatus minic_core_lower_internal_lower_integer_binary_operands(MinicCoreLowerContext *context,
                                                          MinicExpressionId left_id,
                                                          MinicExpressionId right_id,
                                                          MinicType result_type,
                                                          MinicCoreValueId *left_value,
                                                          MinicCoreValueId *right_value) {
    const MinicExpression *left_expression;
    const MinicExpression *right_expression;
    MinicCoreObjectId left_object;
    MinicCoreValueId left_normalized;
    MinicCoreValueId left_source;
    MinicCoreValueId right_normalized;
    MinicCoreValueId right_source;
    MinicCoreLowerStatus status;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || left_value == NULL || right_value == NULL ||
        !minic_type_is_integer(result_type)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    left_expression = minic_c0_program_expression(context->body->program, left_id);
    right_expression = minic_c0_program_expression(context->body->program, right_id);
    if (left_expression == NULL || right_expression == NULL ||
        !minic_type_is_integer(left_expression->type) ||
        !minic_type_is_integer(right_expression->type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }

    status = lower_expression(context, left_id, &left_source);
    if (status != MINIC_CORE_LOWER_OK) {
        (void)fprintf(stderr, "CORE_LOWER_DETAIL marker=M90_HOT_ERROR_DETAIL function=%s stage=integer-binary reason=left-lower status=%d\n",
                      context->source_function != NULL ? context->source_function->name : "?", (int)status);
        return status;
    }
    status = minic_core_lower_internal_append_integer_conversion(
        context, left_expression->span, result_type, left_source, &left_normalized);
    if (status != MINIC_CORE_LOWER_OK) {
        (void)fprintf(stderr, "CORE_LOWER_DETAIL marker=M90_HOT_ERROR_DETAIL function=%s stage=integer-binary reason=left-convert status=%d\n",
                      context->source_function != NULL ? context->source_function->name : "?", (int)status);
        return status;
    }
    status = minic_core_lower_internal_spill_scalar_value(
        context, left_expression->span, result_type, left_normalized, &left_object);
    if (status != MINIC_CORE_LOWER_OK) {
        (void)fprintf(stderr, "CORE_LOWER_DETAIL marker=M90_HOT_ERROR_DETAIL function=%s stage=integer-binary reason=left-spill status=%d\n",
                      context->source_function != NULL ? context->source_function->name : "?", (int)status);
        return status;
    }

    status = lower_expression(context, right_id, &right_source);
    if (status != MINIC_CORE_LOWER_OK) {
        (void)fprintf(stderr, "CORE_LOWER_DETAIL marker=M90_HOT_ERROR_DETAIL function=%s stage=integer-binary reason=right-lower status=%d\n",
                      context->source_function != NULL ? context->source_function->name : "?", (int)status);
        return status;
    }
    status = minic_core_lower_internal_append_integer_conversion(
        context, right_expression->span, result_type, right_source, &right_normalized);
    if (status != MINIC_CORE_LOWER_OK) {
        (void)fprintf(stderr, "CORE_LOWER_DETAIL marker=M90_HOT_ERROR_DETAIL function=%s stage=integer-binary reason=right-convert status=%d\n",
                      context->source_function != NULL ? context->source_function->name : "?", (int)status);
        return status;
    }
    status =
        minic_core_lower_internal_reload_scalar_value(context, left_expression->span, result_type, left_object, left_value);
    if (status != MINIC_CORE_LOWER_OK) {
        (void)fprintf(stderr, "CORE_LOWER_DETAIL marker=M90_HOT_ERROR_DETAIL function=%s stage=integer-binary reason=left-reload status=%d\n",
                      context->source_function != NULL ? context->source_function->name : "?", (int)status);
        return status;
    }
    *right_value = right_normalized;
    return MINIC_CORE_LOWER_OK;
}

MinicCoreLowerStatus minic_core_lower_internal_lower_floating_binary_operands(MinicCoreLowerContext *context,
                                                         MinicExpressionId left_id,
                                                         MinicExpressionId right_id,
                                                         MinicType result_type,
                                                         MinicCoreValueId *left_value,
                                                         MinicCoreValueId *right_value) {
    const MinicExpression *left_expression;
    const MinicExpression *right_expression;
    MinicCoreObjectId left_object;
    MinicCoreValueId left_source;
    MinicCoreValueId right_source;
    MinicCoreLowerStatus status;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || left_value == NULL || right_value == NULL ||
        (!minic_type_is_float(result_type) && !minic_type_is_double(result_type) &&
         !minic_type_is_long_double(result_type))) {
        return MINIC_CORE_LOWER_ERROR;
    }
    left_expression = minic_c0_program_expression(context->body->program, left_id);
    right_expression = minic_c0_program_expression(context->body->program, right_id);
    if (left_expression == NULL || right_expression == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }

    status = minic_core_lower_internal_lower_scalar_assignment_value(context, result_type, left_id, &left_source);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    status = minic_core_lower_internal_spill_scalar_value(
        context, left_expression->span, result_type, left_source, &left_object);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    status = minic_core_lower_internal_lower_scalar_assignment_value(context, result_type, right_id, &right_source);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    status = minic_core_lower_internal_reload_scalar_value(
        context, left_expression->span, result_type, left_object, left_value);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    if (right_source >= context->function->value_count ||
        !minic_type_equal(context->function->values[right_source].type, result_type)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    *right_value = right_source;
    return MINIC_CORE_LOWER_OK;
}

static MinicCoreLowerStatus lower_integer_assignment_value(MinicCoreLowerContext *context,
                                                           MinicType target_type,
                                                           MinicExpressionId expression_id,
                                                           MinicCoreValueId *value_id) {
    const MinicExpression *expression;
    MinicCoreValueId source_value;
    MinicCoreLowerStatus status;
    MinicType result_type;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        value_id == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    if (!minic_c0_integer_assignment_value_type(
            context->body->program, target_type, expression_id, &result_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    status = lower_expression(context, expression_id, &source_value);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    return minic_core_lower_internal_append_integer_conversion(
        context, expression->span, result_type, source_value, value_id);
}

static MinicCoreLowerStatus append_runtime_scalar_call(
    MinicCoreLowerContext *context,
    MinicSourceSpan span,
    const char *name,
    MinicType return_type,
    const MinicType *parameter_types,
    const MinicCoreValueId *argument_values,
    size_t argument_count,
    MinicCoreValueId *value_id) {
    MinicCoreCallArgument arguments[2];
    MinicCoreCalleeId callee_id;
    MinicCoreInstruction instruction;
    size_t argument_begin;
    size_t index;

    if (context == NULL || context->function == NULL || name == NULL ||
        value_id == NULL || argument_count > 2U ||
        (argument_count != 0U && (parameter_types == NULL || argument_values == NULL))) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(arguments, 0, sizeof(arguments));
    for (index = 0U; index < argument_count; ++index) {
        if (argument_values[index] >= context->function->value_count ||
            !minic_type_equal(context->function->values[argument_values[index]].type,
                              parameter_types[index])) {
            return MINIC_CORE_LOWER_ERROR;
        }
        arguments[index].kind = MINIC_CORE_CALL_ARGUMENT_VALUE;
        arguments[index].value.value_id = argument_values[index];
    }
    if (!minic_core_function_add_callee(context->function,
                                        name,
                                        strlen(name),
                                        return_type,
                                        parameter_types,
                                        argument_count,
                                        false,
                                        MINIC_CALLING_CONVENTION_DEFAULT,
                                        &callee_id) ||
        !minic_core_function_append_call_arguments(
            context->function, arguments, argument_count, &argument_begin)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_CALL;
    instruction.span = span;
    instruction.type = return_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.call.callee_id = callee_id;
    instruction.value.call.argument_begin = argument_begin;
    instruction.value.call.argument_count = argument_count;
    instruction.value.call.result_object = MINIC_CORE_OBJECT_INVALID;
    return minic_core_function_append_value_instruction(
               context->function, context->block_id, &instruction, value_id)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

MinicCoreLowerStatus minic_core_lower_internal_append_long_double_conversion(
    MinicCoreLowerContext *context,
    MinicSourceSpan span,
    MinicType target_type,
    MinicType source_type,
    MinicCoreValueId source_value,
    MinicCoreValueId *value_id) {
    const char *helper;
    MinicType parameter_type;
    size_t source_alignment;
    size_t source_size;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || value_id == NULL ||
        source_value >= context->function->value_count) {
        return MINIC_CORE_LOWER_ERROR;
    }
    if (minic_type_equal(target_type, source_type)) {
        *value_id = source_value;
        return MINIC_CORE_LOWER_OK;
    }
    helper = NULL;
    if (minic_type_is_long_double(target_type)) {
        if (minic_type_is_double(source_type)) {
            helper = "__extenddftf2";
        } else if (minic_type_is_float(source_type)) {
            helper = "__extendsftf2";
        } else if (minic_type_is_integer(source_type) &&
                   minic_data_layout_type(core_data_layout(context),
                                          context->body->program,
                                          source_type,
                                          &source_size,
                                          &source_alignment) &&
                   source_size != 0U && source_size <= 8U) {
            (void)source_alignment;
            if (source_size <= 4U) {
                helper = minic_type_is_unsigned_integer(source_type)
                             ? "__floatunsitf"
                             : "__floatsitf";
            } else {
                helper = minic_type_is_unsigned_integer(source_type)
                             ? "__floatunditf"
                             : "__floatditf";
            }
        }
    } else if (minic_type_is_long_double(source_type)) {
        if (minic_type_is_double(target_type)) {
            helper = "__trunctfdf2";
        } else if (minic_type_is_float(target_type)) {
            helper = "__trunctfsf2";
        } else if (minic_type_is_integer(target_type)) {
            MinicCoreLowerStatus status;
            MinicCoreValueId converted_value;
            MinicType helper_return_type;
            size_t target_alignment;
            size_t target_size;

            if (!minic_data_layout_type(core_data_layout(context),
                                        context->body->program,
                                        target_type,
                                        &target_size,
                                        &target_alignment) ||
                target_size == 0U || target_size > 8U) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            (void)target_alignment;
            if (target_size <= 4U) {
                helper = minic_type_is_unsigned_integer(target_type)
                             ? "__fixunstfsi"
                             : "__fixtfsi";
                helper_return_type = minic_type_is_unsigned_integer(target_type)
                                         ? minic_type_unsigned_int()
                                         : minic_type_int();
            } else {
                helper = minic_type_is_unsigned_integer(target_type)
                             ? "__fixunstfdi"
                             : "__fixtfdi";
                helper_return_type = minic_type_is_unsigned_integer(target_type)
                                         ? minic_type_unsigned_long_long()
                                         : minic_type_long_long();
            }
            parameter_type = source_type;
            status = append_runtime_scalar_call(context,
                                                span,
                                                helper,
                                                helper_return_type,
                                                &parameter_type,
                                                &source_value,
                                                1U,
                                                &converted_value);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            if (minic_type_equal(helper_return_type, target_type)) {
                *value_id = converted_value;
                return MINIC_CORE_LOWER_OK;
            }
            return minic_core_lower_internal_append_integer_conversion(
                context, span, target_type, converted_value, value_id);
        }
    }
    if (helper == NULL) return MINIC_CORE_LOWER_UNSUPPORTED;
    parameter_type = source_type;
    return append_runtime_scalar_call(
        context, span, helper, target_type, &parameter_type, &source_value, 1U, value_id);
}

MinicCoreLowerStatus minic_core_lower_internal_append_long_double_binary_helper(
    MinicCoreLowerContext *context,
    MinicSourceSpan span,
    const char *helper,
    MinicCoreValueId left,
    MinicCoreValueId right,
    MinicType return_type,
    MinicCoreValueId *value_id) {
    MinicType parameter_types[2];
    MinicCoreValueId argument_values[2];

    parameter_types[0] = minic_type_long_double();
    parameter_types[1] = minic_type_long_double();
    argument_values[0] = left;
    argument_values[1] = right;
    return append_runtime_scalar_call(context, span, helper, return_type,
                                      parameter_types, argument_values, 2U, value_id);
}

MinicCoreLowerStatus minic_core_lower_internal_lower_scalar_assignment_value(MinicCoreLowerContext *context,
                                                          MinicType target_type,
                                                          MinicExpressionId expression_id,
                                                          MinicCoreValueId *value_id) {
    const MinicExpression *expression;
    MinicCoreInstruction instruction;
    MinicCoreLowerStatus status;
    MinicCoreValueId source_value;
    MinicCoreValueId zero_test;
    MinicCoreValueId truth_value;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || value_id == NULL || !core_memory_scalar_type(target_type)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    /* Parser assignment compatibility intentionally stays strict so it can
       materialize explicit conversion nodes.  Core also uses this helper for
       usual-arithmetic and fixed-argument normalization, where an integer
       operand is semantically converted to binary64 without changing the
       frontend AST contract. */
    if (!minic_c0_assignment_compatible(context->body->program, target_type, expression_id) &&
        !(minic_type_is_double(target_type) && minic_type_is_integer(expression->type)) &&
        !(minic_type_is_long_double(target_type) &&
          (minic_type_is_integer(expression->type) ||
           minic_type_is_float(expression->type) ||
           minic_type_is_double(expression->type) ||
           minic_type_is_long_double(expression->type))) &&
        !((minic_type_is_float(target_type) || minic_type_is_double(target_type)) &&
          minic_type_is_long_double(expression->type))) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    if (minic_type_is_integer(target_type) && minic_type_is_integer(expression->type)) {
        return lower_integer_assignment_value(context, target_type, expression_id, value_id);
    }

    /* Shared integer-zero -> binary32/64 constant fold: a typed numeric
     * conversion of C literal zero is exactly +0.0, not an arbitrary
     * bitcast. Preserve one Core FP constant for all target backends. */
    if (expression->kind == MINIC_EXPRESSION_INTEGER &&
        expression->value.integer_value == 0 &&
        minic_type_is_integer(expression->type) &&
        (minic_type_is_float(target_type) || minic_type_is_double(target_type))) {
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT;
        instruction.span = expression->span;
        instruction.type = target_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.floating_bits = 0U;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
               ? MINIC_CORE_LOWER_OK : MINIC_CORE_LOWER_ERROR;
    }

    status = lower_expression(context, expression_id, &source_value);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    if (source_value >= context->function->value_count) {
        return MINIC_CORE_LOWER_ERROR;
    }
    if (minic_type_is_long_double(target_type)) {
        MinicType source_type;

        if (!core_scalar_expression_value_type(context->body, expression, &source_type) ||
            !minic_type_equal(context->function->values[source_value].type, source_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        return minic_core_lower_internal_append_long_double_conversion(
            context, expression->span, target_type, source_type, source_value, value_id);
    }
    if (minic_type_is_double(target_type)) {
        MinicType source_type;

        if (!core_scalar_expression_value_type(context->body, expression, &source_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        if (!minic_type_equal(context->function->values[source_value].type, source_type)) {
            if (getenv("CORE_FAST_TRACE") != NULL) {
                const MinicType actual_type = context->function->values[source_value].type;
                (void)fprintf(stderr,
                              "CORE_SCALAR_ASSIGN_TRACE function=%s stage=double-source-type "
                              "expr_kind=%d value=%u expected_base=%d expected_ptr=%u "
                              "actual_base=%d actual_ptr=%u expected_qual=%u actual_qual=%u\n",
                              context->source_function != NULL ? context->source_function->name : "?",
                              (int)expression->kind,
                              (unsigned int)source_value,
                              (int)source_type.base_kind,
                              source_type.pointer_depth,
                              (int)actual_type.base_kind,
                              actual_type.pointer_depth,
                              source_type.base_qualifiers,
                              actual_type.base_qualifiers);
            }
            return MINIC_CORE_LOWER_ERROR;
        }
        if (minic_type_is_double(source_type)) {
            if (!minic_type_equal(source_type, target_type)) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            *value_id = source_value;
            return MINIC_CORE_LOWER_OK;
        }
        if (minic_type_is_long_double(source_type)) {
            return minic_core_lower_internal_append_long_double_conversion(
                context, expression->span, target_type, source_type, source_value, value_id);
        }
        if (minic_type_is_integer(source_type)) {
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE;
            instruction.span = expression->span;
            instruction.type = target_type;
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.operand = source_value;
            return minic_core_function_append_value_instruction(
                       context->function, context->block_id, &instruction, value_id)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    if (minic_type_is_float(target_type)) {
        MinicType source_type;

        if (!core_scalar_expression_value_type(context->body, expression, &source_type) ||
            !minic_type_equal(context->function->values[source_value].type, source_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        if (minic_type_is_long_double(source_type)) {
            return minic_core_lower_internal_append_long_double_conversion(
                context, expression->span, target_type, source_type, source_value, value_id);
        }
        if (!minic_type_is_float(source_type) ||
            !minic_type_equal(source_type, target_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        *value_id = source_value;
        return MINIC_CORE_LOWER_OK;
    }
    if (minic_type_is_pointer(target_type)) {
        if (!minic_type_is_pointer(expression->type) &&
            !minic_c0_expression_is_null_pointer_constant_v0(context->body->program,
                                                             expression_id)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        return append_scalar_bitcast(
            context, expression->span, target_type, source_value, value_id);
    }
    if (!minic_type_is_bool_integer(target_type) || !minic_type_is_pointer(expression->type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO;
    instruction.span = expression->span;
    instruction.type = minic_type_int();
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.operand = source_value;
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &instruction, &zero_test)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    instruction.value.operand = zero_test;
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &instruction, &truth_value)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    return minic_core_lower_internal_append_integer_conversion(context, expression->span, target_type, truth_value, value_id);
}

/* Specialized bit-field cases are matched operations; a nonmatch
 * leaves Core untouched and resumes the general expression dispatcher. */
/* Expression operation families live in dedicated Core translation units;
 * their private contract is in core_lower_internal.h. */
/* conditional pruning, void conditionals, omitted-middle GNU ternary, scalar CFG joins. All candidate branches preserve original source order. */
/* comma sequencing and short-circuit logical materialization. All candidate branches preserve original source order. */
/* GNU integer leaf builtins, bit helpers and capability-gated scalar intrinsics. All candidate branches preserve original source order. */
/* floating/integer equality and ordering, target-aware conversions. All candidate branches preserve original source order. */
/* pointer difference and pointer/integer offset semantics. All candidate branches preserve original source order. */
