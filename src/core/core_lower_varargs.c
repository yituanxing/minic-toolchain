#include "core/core_lower_internal.h"
#include "frontend/expression_semantics.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* GNU va_arg is one semantic cursor advance: actual register banks and
 * overflow stack placement remain the selected backend's responsibility. */
MinicCoreLowerStatus minic_core_lower_vararg_scalar(
    MinicCoreLowerContext *context, const MinicExpression *expression,
    MinicCoreValueId *value_id) {
    const MinicExpression *target;
    MinicCoreInstruction operation;
    MinicCoreLowerStatus status;
    MinicCoreValueId list_address;
    MinicCoreValueId cursor_value;
    MinicCoreValueId value_address;
    MinicCoreValueId argument_value;
    MinicCoreValueId one;
    MinicCoreValueId next_cursor;
    MinicType cursor_type;
    MinicType value_pointer_type;
    size_t value_size;
    size_t value_alignment;

    if (context == NULL || expression == NULL || value_id == NULL ||
        context->target == NULL ||
        expression->kind != MINIC_EXPRESSION_BUILTIN_VA_ARG)
        return MINIC_CORE_LOWER_ERROR;
    if (context->target->variadic_scalar_read_kind ==
        MINIC_VARIADIC_SCALAR_READ_UNSUPPORTED)
        return MINIC_CORE_LOWER_UNSUPPORTED;

    target = minic_c0_program_expression(
        context->body->program, expression->value.unary.operand);
    if (target == NULL || target->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_is_pointer(target->type) || minic_type_is_const(target->type) ||
        !minic_type_unqualified(target->type, &cursor_type) ||
        !minic_type_is_pointer(cursor_type) ||
        (!minic_type_is_integer(expression->type) &&
         !minic_type_is_pointer(expression->type) &&
         !minic_type_is_double(expression->type)) ||
        !minic_data_layout_type(core_data_layout(context),
                                context->body->program,
                                expression->type,
                                &value_size,
                                &value_alignment) ||
        value_size == 0U || value_size > 8U || value_alignment > 8U ||
        !minic_type_pointer_to(expression->type, &value_pointer_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    status = lower_address(context, expression->value.unary.operand, &list_address);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    if (context->target->variadic_scalar_read_kind ==
        MINIC_VARIADIC_SCALAR_READ_ABI_CURSOR) {
        /* ABI-owned cursors use one typed IR read; the Target emitter
         * handles the mutable register/overflow cursor representation. */
        (void)memset(&operation, 0, sizeof(operation));
        operation.kind = MINIC_CORE_INSTRUCTION_LOAD;
        operation.span = expression->span;
        operation.type = cursor_type;
        operation.result = MINIC_CORE_VALUE_INVALID;
        operation.value.load.address = list_address;
        operation.value.load.is_volatile = minic_type_is_volatile(target->type);
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &operation,
                &cursor_value)) return MINIC_CORE_LOWER_ERROR;
        (void)memset(&operation, 0, sizeof(operation));
        operation.kind = MINIC_CORE_INSTRUCTION_VARIADIC_NEXT_SCALAR;
        operation.span = expression->span;
        operation.type = expression->type;
        operation.result = MINIC_CORE_VALUE_INVALID;
        operation.value.operand = cursor_value;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &operation,
                   value_id)
                   ? MINIC_CORE_LOWER_OK : MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&operation, 0, sizeof(operation));
    operation.kind = MINIC_CORE_INSTRUCTION_LOAD;
    operation.span = expression->span;
    operation.type = cursor_type;
    operation.result = MINIC_CORE_VALUE_INVALID;
    operation.value.load.address = list_address;
    operation.value.load.is_volatile = minic_type_is_volatile(target->type);
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &operation, &cursor_value)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    status = append_scalar_bitcast(
        context, expression->span, value_pointer_type, cursor_value, &value_address);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    if (context->target->variadic_scalar_read_kind !=
            MINIC_VARIADIC_SCALAR_READ_LINEAR_SLOT ||
        context->target->variadic_linear_slot_bytes == 0U)
        return MINIC_CORE_LOWER_UNSUPPORTED;
    (void)memset(&operation, 0, sizeof(operation));
    operation.kind = MINIC_CORE_INSTRUCTION_LOAD;
    operation.span = expression->span;
    operation.type = expression->type;
    operation.result = MINIC_CORE_VALUE_INVALID;
    operation.value.load.address = value_address;
    operation.value.load.is_volatile = false;
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &operation, &argument_value)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&operation, 0, sizeof(operation));
    operation.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
    operation.span = expression->span;
    operation.type = minic_type_int();
    operation.result = MINIC_CORE_VALUE_INVALID;
    operation.value.integer_value = 1;
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &operation, &one)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&operation, 0, sizeof(operation));
    operation.kind = MINIC_CORE_INSTRUCTION_POINTER_OFFSET;
    operation.span = expression->span;
    operation.type = cursor_type;
    operation.result = MINIC_CORE_VALUE_INVALID;
    operation.value.pointer_offset.base = cursor_value;
    operation.value.pointer_offset.index = one;
    operation.value.pointer_offset.element_size =
        context->target->variadic_linear_slot_bytes;
    operation.value.pointer_offset.subtract = false;
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &operation, &next_cursor)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&operation, 0, sizeof(operation));
    operation.kind = MINIC_CORE_INSTRUCTION_STORE;
    operation.span = expression->span;
    operation.type = minic_type_void();
    operation.result = MINIC_CORE_VALUE_INVALID;
    operation.value.store.address = list_address;
    operation.value.store.stored_value = next_cursor;
    operation.value.store.is_volatile = minic_type_is_volatile(target->type);
    if (!minic_core_function_append_effect_instruction(
            context->function, context->block_id, &operation)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    *value_id = argument_value;
    return MINIC_CORE_LOWER_OK;
}

/* Stateful GNU variadic builtins: Core produces semantic effects while
 * TargetInfo selects the compatible ABI cursor policy. */
MinicCoreLowerStatus minic_core_lower_vararg_statement(
    MinicCoreLowerContext *context, const MinicExpression *expression) {
    MinicExpressionId target_id;
    if (context == NULL || expression == NULL || context->target == NULL)
        return MINIC_CORE_LOWER_ERROR;

    /* M123_VARIADIC_ARGUMENT_ADDRESS: GNU va builtins are parsed and
       type-checked by frontend/Sema. Core owns their target-neutral execution
       semantics while the selected ABI/backend owns the register save area.
       The current frontend va_list model is a modifiable pointer lvalue. */
    if (expression->kind == MINIC_EXPRESSION_BUILTIN_VA_START) {
        const MinicExpression *target;
        MinicCoreInstruction instruction;
        MinicCoreLowerStatus status;
        MinicCoreValueId cursor_value;
        MinicCoreValueId target_address;
        MinicType value_type;

        target_id = expression->value.unary.operand;
        target = minic_c0_program_expression(context->body->program, target_id);
        if (context->source_function == NULL || !context->source_function->is_variadic ||
            target == NULL || target->value_category != MINIC_VALUE_LVALUE ||
            !minic_type_is_pointer(target->type) || minic_type_is_const(target->type) ||
            !minic_type_unqualified(target->type, &value_type) ||
            !minic_type_is_pointer(value_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_address(context, target_id, &target_address);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS;
        instruction.span = expression->span;
        instruction.type = value_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &cursor_value)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
        instruction.span = expression->span;
        instruction.type = minic_type_void();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.store.address = target_address;
        instruction.value.store.stored_value = cursor_value;
        instruction.value.store.is_volatile = minic_type_is_volatile(target->type);
        return minic_core_function_append_effect_instruction(
                   context->function, context->block_id, &instruction)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_BUILTIN_VA_COPY) {
        /* Transitional pointer-assignment compatibility is not proof of
         * structured va_list value-copy correctness. The real ABI-specific
         * copy must eventually become a distinct semantic IR operation. */
        if (!context->target->variadic_pointer_assignment_compatible)
            return MINIC_CORE_LOWER_UNSUPPORTED;
        return minic_core_lower_assignment_pair(context,
                                     expression->value.binary.left,
                                     expression->value.binary.right,
                                     expression->span,
                                     NULL);
    }
    if (expression->kind == MINIC_EXPRESSION_BUILTIN_VA_END) {
        MinicCoreValueId discarded_address;

        return lower_address(context, expression->value.unary.operand, &discarded_address);
    }

    return MINIC_CORE_LOWER_UNSUPPORTED;
}
