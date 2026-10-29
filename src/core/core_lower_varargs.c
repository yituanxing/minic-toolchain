#include "core/core_lower_internal.h"
#include "frontend/expression_semantics.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* GNU va_arg is one semantic cursor advance: actual register banks and
 * overflow stack placement remain the selected backend's responsibility. */

/* A 2*XLEN RV32I va_arg needs an 8-byte-aligned cursor; this generic
 * Core SSA alignment is selected by the target DataLayout, never
 * hardcoded into the frontend or emulated inside a test runtime. */
static MinicCoreLowerStatus linear_vararg_align8(
    MinicCoreLowerContext *context, MinicSourceSpan span,
    MinicCoreValueId cursor, MinicType cursor_type,
    MinicCoreValueId *result) {
    MinicCoreInstruction ins;
    MinicCoreValueId bits, seven, rounded, mask, masked;
    const MinicType word = minic_type_unsigned_int();
    MinicCoreLowerStatus status;
    if (context == NULL || result == NULL ||
        core_data_layout(context)->pointer_size != 4U)
        return MINIC_CORE_LOWER_UNSUPPORTED;
    status = append_scalar_bitcast(context, span, word, cursor, &bits);
    if (status != MINIC_CORE_LOWER_OK) return status;
    (void)memset(&ins, 0, sizeof(ins));
    ins.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
    ins.span = span;
    ins.type = word;
    ins.result = MINIC_CORE_VALUE_INVALID;
    ins.value.integer_value = 7;
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &ins, &seven))
        return MINIC_CORE_LOWER_ERROR;
    (void)memset(&ins, 0, sizeof(ins));
    ins.kind = MINIC_CORE_INSTRUCTION_INTEGER_ADD;
    ins.span = span;
    ins.type = word;
    ins.result = MINIC_CORE_VALUE_INVALID;
    ins.value.binary.left = bits;
    ins.value.binary.right = seven;
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &ins, &rounded))
        return MINIC_CORE_LOWER_ERROR;
    (void)memset(&ins, 0, sizeof(ins));
    ins.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
    ins.span = span;
    ins.type = word;
    ins.result = MINIC_CORE_VALUE_INVALID;
    ins.value.integer_value = -8;
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &ins, &mask))
        return MINIC_CORE_LOWER_ERROR;
    (void)memset(&ins, 0, sizeof(ins));
    ins.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND;
    ins.span = span;
    ins.type = word;
    ins.result = MINIC_CORE_VALUE_INVALID;
    ins.value.binary.left = rounded;
    ins.value.binary.right = mask;
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &ins, &masked))
        return MINIC_CORE_LOWER_ERROR;
    return append_scalar_bitcast(
        context, span, cursor_type, masked, result);
}

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
        (context->target->variadic_scalar_read_kind ==
            MINIC_VARIADIC_SCALAR_READ_LINEAR_SLOT &&
         context->target->variadic_linear_slot_bytes == 4U &&
         (value_size > 8U || value_alignment > 8U)) ||
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
    if (context->target->variadic_scalar_read_kind ==
            MINIC_VARIADIC_SCALAR_READ_LINEAR_SLOT &&
        context->target->variadic_linear_slot_bytes == 4U &&
        value_alignment == 8U) {
        MinicCoreValueId aligned;
        status = linear_vararg_align8(
            context, expression->span, cursor_value, cursor_type, &aligned);
        if (status != MINIC_CORE_LOWER_OK) return status;
        cursor_value = aligned;
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
    operation.value.integer_value = (int64_t)((value_size +
        context->target->variadic_linear_slot_bytes - 1U) /
        context->target->variadic_linear_slot_bytes);
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
        instruction.value.variadic_address.copy_destination = false;
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
        /* An ABI-state cursor copy requires independently mutable storage.
         * The target validates the source cursor type and emits its native
         * state transfer; Core only models allocation/copy/writeback. */
        if (context->target->variadic_copy_kind == MINIC_VARIADIC_COPY_ABI_STATE) {
            const MinicExpression *target;
            const MinicExpression *source;
            MinicCoreValueId dest_address, source_cursor, new_cursor;
            MinicCoreInstruction instruction;
            MinicCoreLowerStatus status;
            MinicType cursor_type;
            target = minic_c0_program_expression(
                context->body->program, expression->value.binary.left);
            source = minic_c0_program_expression(
                context->body->program, expression->value.binary.right);
            if (target == NULL || source == NULL ||
                target->value_category != MINIC_VALUE_LVALUE ||
                !minic_type_is_pointer(target->type) ||
                !minic_target_info_variadic_state_copy_type_supported(
                    context->target, target->type) ||
                !minic_type_equal(target->type, source->type) ||
                !minic_type_unqualified(target->type, &cursor_type))
                return MINIC_CORE_LOWER_UNSUPPORTED;
            status = lower_address(context, expression->value.binary.left, &dest_address);
            if (status != MINIC_CORE_LOWER_OK) return status;
            status = lower_expression(context, expression->value.binary.right, &source_cursor);
            if (status != MINIC_CORE_LOWER_OK) return status;
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS;
            instruction.span = expression->span;
            instruction.type = cursor_type;
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.variadic_address.copy_destination = true;
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &instruction, &new_cursor))
                return MINIC_CORE_LOWER_ERROR;
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_VARIADIC_COPY;
            instruction.span = expression->span;
            instruction.type = minic_type_void();
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.variadic_copy.destination = new_cursor;
            instruction.value.variadic_copy.source = source_cursor;
            if (!minic_core_function_append_effect_instruction(
                    context->function, context->block_id, &instruction))
                return MINIC_CORE_LOWER_ERROR;
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
            instruction.span = expression->span;
            instruction.type = minic_type_void();
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.store.address = dest_address;
            instruction.value.store.stored_value = new_cursor;
            instruction.value.store.is_volatile = minic_type_is_volatile(target->type);
            return minic_core_function_append_effect_instruction(
                       context->function, context->block_id, &instruction)
                       ? MINIC_CORE_LOWER_OK : MINIC_CORE_LOWER_ERROR;
        }
        /* Preserve existing scalar-cursor policy. Unknown ABI policies
         * fail closed rather than treating a structured cursor as pointer. */
        if (context->target->variadic_copy_kind !=
                MINIC_VARIADIC_COPY_SCALAR_ASSIGNMENT)
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
