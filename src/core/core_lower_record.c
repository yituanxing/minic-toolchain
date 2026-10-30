#include "core/core_lower_internal.h"
#include "core/linear_vararg_cursor.h"
#include "core/local_facts.h"
#include "core/cfg_constant_calls.h"
#include "frontend/const_eval.h"
#include "frontend/expression_semantics.h"
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Aggregate record ownership: materialization, conditional records, copies,
 * and variadic record values. Machine ABI stays inside Target. */

/* BATCH_B_RECORD_COMPOUND_LITERAL_OBJECT: a block-scope record compound
   literal already owns one frontend local backing object and one initializer
   block. Materialize that exact semantic object so all aggregate consumers
   (copy, call, return) share one ownership seam. */
MinicCoreLowerStatus minic_core_lower_record_compound_literal_object(
    MinicCoreLowerContext *context,
    const MinicExpression *expression,
    MinicCoreObjectId *object_id) {
    const MinicBlock *initializer_block;
    MinicCoreLowerStatus status;
    bool terminated;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        expression == NULL || object_id == NULL ||
        expression->kind != MINIC_EXPRESSION_COMPOUND_LITERAL ||
        !minic_type_is_record(expression->type)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    initializer_block = minic_c0_program_block(
        context->body->program, expression->value.compound_literal.initializer_block);
    if (initializer_block == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    status = minic_core_lower_internal_lower_block(context, initializer_block, &terminated);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    if (terminated) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    status = minic_core_lower_internal_local_object(
        context, expression->value.compound_literal.local_id, object_id);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    if (*object_id >= context->function->object_count ||
        !minic_type_equal(context->function->objects[*object_id].type, expression->type)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    return MINIC_CORE_LOWER_OK;
}

/* M80_ADDRESS_BACKED_RECORD_COPY: aggregate values stay address-backed in
   Core. Resolve the subset whose storage already exists: record lvalues,
   lvalue-read wrappers, and GNU statement expressions whose final record value
   is itself address-backed. Calls/conditionals remain fail-closed. */
static MinicCoreLowerStatus lower_record_value_address(MinicCoreLowerContext *context,
                                                       MinicExpressionId expression_id,
                                                       MinicCoreValueId *address_id) {
    const MinicExpression *expression;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        address_id == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL || !minic_type_is_record(expression->type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    /* M109_CHAINED_RECORD_ASSIGNMENT_VALUE: an aggregate assignment is an
       rvalue whose bytes are the fully evaluated RHS. Keep that value
       address-backed: snapshot the RHS before evaluating the destination, copy
       the snapshot to the destination, and return the snapshot address. This
       composes chained assignments without aggregate SSA or target ABI rules. */
    if (expression->kind == MINIC_EXPRESSION_ASSIGNMENT) {
        const MinicExpression *source;
        const MinicExpression *target;
        MinicCoreInstruction operation;
        MinicCoreObjectId snapshot_object;
        MinicCoreValueId destination_address;
        MinicCoreValueId snapshot_address;
        MinicCoreValueId source_address;
        MinicCoreLowerStatus status;
        MinicType expression_type;
        MinicType pointer_type;
        MinicType source_type;
        MinicType target_type;

        target = minic_c0_program_expression(
            context->body->program, expression->value.binary.left);
        source = minic_c0_program_expression(
            context->body->program, expression->value.binary.right);
        if (target == NULL || source == NULL ||
            target->value_category != MINIC_VALUE_LVALUE ||
            !minic_type_is_record(target->type) || !minic_type_is_record(source->type) ||
            minic_type_is_const(target->type) || minic_type_is_volatile(target->type) ||
            minic_type_is_volatile(source->type) ||
            !minic_type_unqualified(expression->type, &expression_type) ||
            !minic_type_unqualified(target->type, &target_type) ||
            !minic_type_unqualified(source->type, &source_type) ||
            !minic_type_equal(expression_type, target_type) ||
            !minic_type_equal(expression_type, source_type) ||
            !minic_type_is_record(expression_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        /* M127_CHAINED_RECORD_MATERIALIZED_RHS: the value of a record
           assignment is the fully evaluated RHS snapshot.  Route the RHS
           through the materializing aggregate seam so direct record-returning
           calls, record conditionals and compound literals compose with
           chained assignment exactly like ordinary address-backed records. */
        status = minic_core_lower_internal_record_materialized_address(
            context, expression->value.binary.right, &source_address);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (!minic_core_function_add_object(
                context->function, expression->span, expression_type, &snapshot_object) ||
            !minic_type_pointer_to(expression_type, &pointer_type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&operation, 0, sizeof(operation));
        operation.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
        operation.span = expression->span;
        operation.type = pointer_type;
        operation.result = MINIC_CORE_VALUE_INVALID;
        operation.value.object_id = snapshot_object;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &operation, &snapshot_address)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&operation, 0, sizeof(operation));
        operation.kind = MINIC_CORE_INSTRUCTION_RECORD_COPY;
        operation.span = expression->span;
        operation.type = expression_type;
        operation.result = MINIC_CORE_VALUE_INVALID;
        operation.value.record_copy.destination_address = snapshot_address;
        operation.value.record_copy.source_address = source_address;
        if (!minic_core_function_append_effect_instruction(
                context->function, context->block_id, &operation)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = lower_address(
            context, expression->value.binary.left, &destination_address);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        (void)memset(&operation, 0, sizeof(operation));
        operation.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
        operation.span = expression->span;
        operation.type = pointer_type;
        operation.result = MINIC_CORE_VALUE_INVALID;
        operation.value.object_id = snapshot_object;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &operation, &snapshot_address)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&operation, 0, sizeof(operation));
        operation.kind = MINIC_CORE_INSTRUCTION_RECORD_COPY;
        operation.span = expression->span;
        operation.type = expression_type;
        operation.result = MINIC_CORE_VALUE_INVALID;
        operation.value.record_copy.destination_address = destination_address;
        operation.value.record_copy.source_address = snapshot_address;
        if (!minic_core_function_append_effect_instruction(
                context->function, context->block_id, &operation)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        *address_id = snapshot_address;
        return MINIC_CORE_LOWER_OK;
    }
    if (!minic_c0_record_value_is_address_backed(
            context->body->program, expression_id)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    /* M88_RECORD_COMPOUND_LITERAL_ADDRESS: expose the shared semantic backing
       object through the address-backed aggregate seam. */
    if (expression->kind == MINIC_EXPRESSION_COMPOUND_LITERAL) {
        MinicCoreInstruction address_instruction;
        MinicCoreObjectId object_id;
        MinicCoreLowerStatus status;
        MinicType pointer_type;

        status = minic_core_lower_record_compound_literal_object(context, expression, &object_id);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (!minic_type_pointer_to(expression->type, &pointer_type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&address_instruction, 0, sizeof(address_instruction));
        address_instruction.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
        address_instruction.span = expression->span;
        address_instruction.type = pointer_type;
        address_instruction.result = MINIC_CORE_VALUE_INVALID;
        address_instruction.value.object_id = object_id;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &address_instruction, address_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->value_category == MINIC_VALUE_LVALUE) {
        return lower_address(context, expression_id, address_id);
    }
    if (expression->value_category != MINIC_VALUE_RVALUE) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    if (expression->kind == MINIC_EXPRESSION_LVALUE_READ) {
        const MinicExpression *operand = minic_c0_program_expression(
            context->body->program, expression->value.unary.operand);
        if (operand == NULL || !minic_type_is_record(operand->type) ||
            operand->type.record_id != expression->type.record_id) {
            return MINIC_CORE_LOWER_ERROR;
        }
        return lower_record_value_address(context, expression->value.unary.operand, address_id);
    }
    if (expression->kind == MINIC_EXPRESSION_STATEMENT) {
        const MinicBlock *block;
        const MinicExpression *result;
        MinicCoreLowerStatus status;
        bool terminated;

        if (expression->value.statement_expression.result == MINIC_EXPRESSION_INVALID) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        block = minic_c0_program_block(
            context->body->program, expression->value.statement_expression.block);
        result = minic_c0_program_expression(
            context->body->program, expression->value.statement_expression.result);
        if (block == NULL || result == NULL || !minic_type_is_record(result->type) ||
            result->type.record_id != expression->type.record_id) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = minic_core_lower_internal_lower_block(context, block, &terminated);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (terminated) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        /* A record-valued GNU statement expression may end in a
           materialized aggregate producer such as a conditional or compound
           literal. The statement-expression owner has already sequenced the
           block; hand its result to the aggregate materialization owner rather
           than requiring the result itself to be pre-address-backed. */
        return minic_core_lower_internal_record_materialized_address(
            context, expression->value.statement_expression.result, address_id);
    }
    return MINIC_CORE_LOWER_UNSUPPORTED;
}

/* M114_RECORD_CONDITIONAL_OBJECT: record values remain address-backed in Core.
   Materialize one private result object and copy exactly the selected arm into
   it. Arms may be ordinary address-backed records, compound literals, direct
   record-returning calls, or nested record conditionals. */
/* SysV/AAPCS64 register-save-area varargs classify a one-scalar-field record
 * as one scalar argument. Reuse the target-owned scalar cursor operation, but
 * materialize the value into a fresh record object in Core. The rule is
 * deliberately narrow: multi-field, union, padded, and bit-field aggregates
 * need the target's full aggregate ABI classifier and remain rejected. */
static MinicCoreLowerStatus lower_abi_cursor_scalar_record_va_arg(
    MinicCoreLowerContext *context, const MinicExpression *expression,
    MinicType record_type, MinicCoreObjectId *object_id) {
    const MinicRecord *record;
    const MinicRecordField *field;
    MinicCoreInstruction instruction;
    MinicCoreValueId cursor;
    MinicCoreValueId value;
    MinicCoreValueId record_address;
    MinicCoreValueId field_address;
    MinicType record_pointer_type;
    MinicType field_pointer_type;
    size_t record_size;
    size_t record_alignment;
    size_t field_size;
    size_t field_alignment;
    MinicCoreLowerStatus status;

    record = minic_c0_program_record(context->body->program, record_type.record_id);
    if (record == NULL || !record->is_complete || record->is_union ||
        record->field_count != 1U || record->fields == NULL ||
        expression->value.unary.operand == MINIC_EXPRESSION_INVALID)
        return MINIC_CORE_LOWER_UNSUPPORTED;
    field = &record->fields[0];
    if (field->is_array || field->is_bit_field || field->is_flexible_array ||
        field->is_zero_length_array || field->is_anonymous_member ||
        (!minic_type_is_integer(field->type) &&
         !minic_type_is_pointer(field->type) &&
         !minic_type_is_double(field->type)) ||
        !minic_data_layout_type(core_data_layout(context),
                                context->body->program, record_type,
                                &record_size, &record_alignment) ||
        !minic_data_layout_type(core_data_layout(context),
                                context->body->program, field->type,
                                &field_size, &field_alignment) ||
        record_size != field_size || record_alignment != field_alignment ||
        record_size == 0U || record_size > 8U ||
        !minic_type_pointer_to(record_type, &record_pointer_type) ||
        !minic_type_pointer_to(field->type, &field_pointer_type))
        return MINIC_CORE_LOWER_UNSUPPORTED;

    status = minic_core_lower_abi_va_state_pointer(
        context, expression->value.unary.operand, &cursor);
    if (status != MINIC_CORE_LOWER_OK) return status;
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_VARIADIC_NEXT_SCALAR;
    instruction.span = expression->span;
    instruction.type = field->type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.operand = cursor;
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &instruction, &value))
        return MINIC_CORE_LOWER_ERROR;

    if (!minic_core_function_add_object(
            context->function, expression->span, record_type, object_id))
        return MINIC_CORE_LOWER_ERROR;
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
    instruction.span = expression->span;
    instruction.type = record_pointer_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.object_id = *object_id;
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &instruction, &record_address))
        return MINIC_CORE_LOWER_ERROR;
    status = append_scalar_bitcast(context, expression->span,
                                   field_pointer_type, record_address,
                                   &field_address);
    if (status != MINIC_CORE_LOWER_OK) return status;
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
    instruction.span = expression->span;
    instruction.type = minic_type_void();
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.store.address = field_address;
    instruction.value.store.stored_value = value;
    instruction.value.store.is_volatile = false;
    return minic_core_function_append_effect_instruction(
               context->function, context->block_id, &instruction)
               ? MINIC_CORE_LOWER_OK : MINIC_CORE_LOWER_ERROR;
}

static MinicCoreLowerStatus lower_record_va_arg_object(
    MinicCoreLowerContext *context,
    const MinicExpression *expression,
    MinicCoreObjectId *object_id) {
    const MinicExpression *target;
    MinicCoreInstruction operation;
    MinicCoreLowerStatus status;
    MinicCoreValueId list_address;
    MinicCoreValueId cursor_value;
    MinicCoreValueId source_address;
    MinicCoreValueId destination_address;
    MinicType cursor_type;
    MinicType record_type;
    MinicType record_pointer_type;
    size_t value_size;
    size_t value_alignment;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || context->target == NULL ||
        expression == NULL || object_id == NULL ||
        expression->kind != MINIC_EXPRESSION_BUILTIN_VA_ARG ||
        !minic_type_unqualified(expression->type, &record_type) ||
        !minic_type_is_record(record_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    if (context->target->variadic_scalar_read_kind ==
            MINIC_VARIADIC_SCALAR_READ_ABI_CURSOR)
        return lower_abi_cursor_scalar_record_va_arg(
            context, expression, record_type, object_id);
    if (context->target->variadic_scalar_read_kind !=
            MINIC_VARIADIC_SCALAR_READ_LINEAR_SLOT ||
        context->target->variadic_linear_slot_bytes == 0U)
        return MINIC_CORE_LOWER_UNSUPPORTED;
    /* A structured GP/FP cursor needs a target-specific aggregate ABI
     * classifier; reinterpreting its tag pointer as a linear record slot
     * would silently read unrelated bytes. Reject until it is modeled. */
    target = minic_c0_program_expression(
        context->body->program, expression->value.unary.operand);
    if (target == NULL || target->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_is_pointer(target->type) || minic_type_is_const(target->type) ||
        !minic_type_unqualified(target->type, &cursor_type) ||
        !minic_type_is_pointer(cursor_type) ||
        !minic_data_layout_type(core_data_layout(context),
                                context->body->program,
                                record_type,
                                &value_size,
                                &value_alignment) ||
        value_size == 0U || value_size > 8U || value_alignment == 0U ||
        value_alignment > 8U ||
        !minic_type_pointer_to(record_type, &record_pointer_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }

    status = lower_address(context, expression->value.unary.operand, &list_address);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
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

    /* A linear cursor is a sequence of ABI-sized slots, not one slot per
     * source aggregate. Align before reading 2*XLEN-aligned records (RV32)
     * and advance by ceil(record_size / slot_bytes) after the copy. */
    status = minic_core_linear_vararg_prepare_cursor(
        context, expression->span, cursor_value, cursor_type,
        value_alignment, &cursor_value);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }

    status = append_scalar_bitcast(
        context, expression->span, record_pointer_type, cursor_value, &source_address);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    if (!minic_core_function_add_object(
            context->function, expression->span, record_type, object_id)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&operation, 0, sizeof(operation));
    operation.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
    operation.span = expression->span;
    operation.type = record_pointer_type;
    operation.result = MINIC_CORE_VALUE_INVALID;
    operation.value.object_id = *object_id;
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &operation, &destination_address)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&operation, 0, sizeof(operation));
    operation.kind = MINIC_CORE_INSTRUCTION_RECORD_COPY;
    operation.span = expression->span;
    operation.type = record_type;
    operation.result = MINIC_CORE_VALUE_INVALID;
    operation.value.record_copy.destination_address = destination_address;
    operation.value.record_copy.source_address = source_address;
    if (!minic_core_function_append_effect_instruction(
            context->function, context->block_id, &operation)) {
        return MINIC_CORE_LOWER_ERROR;
    }

    return minic_core_linear_vararg_advance(
        context, expression->span, cursor_value, list_address,
        cursor_type, value_size, minic_type_is_volatile(target->type));
}

MinicCoreLowerStatus minic_core_lower_internal_record_materialized_address(
    MinicCoreLowerContext *context,
    MinicExpressionId expression_id,
    MinicCoreValueId *address_id) {
    const MinicExpression *expression;
    MinicCoreInstruction instruction;
    MinicCoreObjectId object_id;
    MinicCoreLowerStatus status;
    MinicType pointer_type;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || address_id == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL || !minic_type_is_record(expression->type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    if (expression->kind == MINIC_EXPRESSION_MEMBER &&
        expression->value_category == MINIC_VALUE_RVALUE) {
        const MinicExpression *base;
        const MinicRecord *record;
        const MinicRecordField *field;
        MinicCoreValueId base_address;

        base = minic_c0_program_expression(
            context->body->program, expression->value.member.base);
        record = minic_c0_program_record(
            context->body->program, expression->value.member.record_id);
        field = minic_c0_record_field(record, expression->value.member.field_index);
        if (base == NULL || field == NULL ||
            !minic_c0_record_value_is_copy_source(
                context->body->program, expression->value.member.base) ||
            base->value_category != MINIC_VALUE_RVALUE ||
            !minic_type_is_record(base->type) ||
            base->type.record_id != expression->value.member.record_id ||
            !minic_type_is_record(field->type) || field->is_array ||
            field->type.record_id != expression->type.record_id)
            return MINIC_CORE_LOWER_UNSUPPORTED;
        status = minic_core_lower_internal_record_materialized_address(
            context, expression->value.member.base, &base_address);
        if (status != MINIC_CORE_LOWER_OK)
            return status;
        return minic_core_lower_internal_append_field_address(
            context, expression->span, base_address,
            expression->value.member.record_id,
            expression->value.member.field_index, expression->type, address_id);
    }
    if (expression->kind == MINIC_EXPRESSION_CAST) {
        const MinicExpressionId operand_id = expression->value.unary.operand;
        const MinicExpression *operand = minic_c0_program_expression(
            context->body->program, operand_id);
        if (operand == NULL || !minic_type_is_record(operand->type) ||
            operand->type.record_id != expression->type.record_id)
            return MINIC_CORE_LOWER_UNSUPPORTED;
        return minic_core_lower_internal_record_materialized_address(
            context, operand_id, address_id);
    }
    if (expression->kind == MINIC_EXPRESSION_STATEMENT) {
        const MinicBlock *statement_block;
        const MinicExpression *statement_result;
        bool terminated;

        if (expression->value.statement_expression.result == MINIC_EXPRESSION_INVALID) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        statement_block = minic_c0_program_block(
            context->body->program, expression->value.statement_expression.block);
        statement_result = minic_c0_program_expression(
            context->body->program, expression->value.statement_expression.result);
        if (statement_block == NULL || statement_result == NULL ||
            !minic_type_is_record(statement_result->type) ||
            statement_result->type.record_id != expression->type.record_id) {
            return MINIC_CORE_LOWER_ERROR;
        }
        terminated = false;
        status = minic_core_lower_internal_lower_block(context, statement_block, &terminated);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (terminated) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        return minic_core_lower_internal_record_materialized_address(
            context, expression->value.statement_expression.result, address_id);
    }
    if (expression->kind == MINIC_EXPRESSION_BUILTIN_VA_ARG) {
        status = lower_record_va_arg_object(context, expression, &object_id);
    } else if (expression->kind == MINIC_EXPRESSION_CONDITIONAL) {
        status = minic_core_lower_record_conditional_object(context, expression, &object_id);
    } else if (expression->kind == MINIC_EXPRESSION_CALL &&
               expression->value.call.function_id != MINIC_FUNCTION_INVALID) {
        status = minic_core_lower_direct_record_call_object(context, expression, &object_id);
    } else if (expression->kind == MINIC_EXPRESSION_CALL) {
        status = minic_core_lower_indirect_record_call_object(context, expression, &object_id);
    } else if (expression->kind == MINIC_EXPRESSION_COMPOUND_LITERAL) {
        status = minic_core_lower_record_compound_literal_object(context, expression, &object_id);
    } else {
        return lower_record_value_address(context, expression_id, address_id);
    }
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    if (object_id >= context->function->object_count ||
        !minic_type_pointer_to(context->function->objects[object_id].type, &pointer_type)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
    instruction.span = expression->span;
    instruction.type = pointer_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.object_id = object_id;
    return minic_core_function_append_value_instruction(
               context->function, context->block_id, &instruction, address_id)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

MinicCoreLowerStatus minic_core_lower_record_conditional_object(
    MinicCoreLowerContext *context,
    const MinicExpression *expression,
    MinicCoreObjectId *result_object) {
    /* FIXME(MC-036): both arms currently share local constant facts without
       the entry restore / exit meet used by scalar conditional lowering. */
    const MinicExpression *false_expression;
    const MinicExpression *true_expression;
    MinicCoreBlockId false_block;
    MinicCoreBlockId merge_block;
    MinicCoreBlockId true_block;
    MinicCoreInstruction operation;
    MinicCoreLowerStatus status;
    MinicCoreValueId destination_address;
    MinicCoreValueId source_address;
    MinicType false_type;
    MinicType pointer_type;
    MinicType result_type;
    MinicType true_type;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || expression == NULL || result_object == NULL ||
        expression->kind != MINIC_EXPRESSION_CONDITIONAL ||
        expression->value.conditional.uses_condition_value ||
        expression->value.conditional.when_true == MINIC_EXPRESSION_INVALID ||
        expression->value.conditional.when_false == MINIC_EXPRESSION_INVALID ||
        !minic_type_is_record(expression->type) ||
        !minic_type_unqualified(expression->type, &result_type) ||
        !minic_type_is_record(result_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    true_expression = minic_c0_program_expression(
        context->body->program, expression->value.conditional.when_true);
    false_expression = minic_c0_program_expression(
        context->body->program, expression->value.conditional.when_false);
    if (true_expression == NULL || false_expression == NULL ||
        !minic_type_is_record(true_expression->type) ||
        !minic_type_is_record(false_expression->type) ||
        !minic_type_unqualified(true_expression->type, &true_type) ||
        !minic_type_unqualified(false_expression->type, &false_type) ||
        !minic_type_equal(result_type, true_type) ||
        !minic_type_equal(result_type, false_type) ||
        !minic_type_pointer_to(result_type, &pointer_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    if (!minic_core_function_add_object(
            context->function, expression->span, result_type, result_object) ||
        !minic_core_function_add_block(context->function, &true_block) ||
        !minic_core_function_add_block(context->function, &false_block) ||
        !minic_core_function_add_block(context->function, &merge_block)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    status = minic_core_lower_internal_lower_condition_branch(context,
                                    expression->value.conditional.condition,
                                    expression->span,
                                    true_block,
                                    false_block);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }

    context->block_id = true_block;
    status = minic_core_lower_internal_record_materialized_address(
        context, expression->value.conditional.when_true, &source_address);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    (void)memset(&operation, 0, sizeof(operation));
    operation.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
    operation.span = true_expression->span;
    operation.type = pointer_type;
    operation.result = MINIC_CORE_VALUE_INVALID;
    operation.value.object_id = *result_object;
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &operation, &destination_address)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&operation, 0, sizeof(operation));
    operation.kind = MINIC_CORE_INSTRUCTION_RECORD_COPY;
    operation.span = true_expression->span;
    operation.type = result_type;
    operation.result = MINIC_CORE_VALUE_INVALID;
    operation.value.record_copy.destination_address = destination_address;
    operation.value.record_copy.source_address = source_address;
    if (!minic_core_function_append_effect_instruction(
            context->function, context->block_id, &operation)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    status = minic_core_lower_internal_set_branch(context, context->block_id, expression->span, merge_block);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }

    context->block_id = false_block;
    status = minic_core_lower_internal_record_materialized_address(
        context, expression->value.conditional.when_false, &source_address);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    (void)memset(&operation, 0, sizeof(operation));
    operation.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
    operation.span = false_expression->span;
    operation.type = pointer_type;
    operation.result = MINIC_CORE_VALUE_INVALID;
    operation.value.object_id = *result_object;
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &operation, &destination_address)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&operation, 0, sizeof(operation));
    operation.kind = MINIC_CORE_INSTRUCTION_RECORD_COPY;
    operation.span = false_expression->span;
    operation.type = result_type;
    operation.result = MINIC_CORE_VALUE_INVALID;
    operation.value.record_copy.destination_address = destination_address;
    operation.value.record_copy.source_address = source_address;
    if (!minic_core_function_append_effect_instruction(
            context->function, context->block_id, &operation)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    status = minic_core_lower_internal_set_branch(context, context->block_id, expression->span, merge_block);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }

    context->block_id = merge_block;
    return MINIC_CORE_LOWER_OK;
}

/* BATCH_M_RECORD_LOAD: turn a record value with a materializable address
   into a private Core snapshot object.  This includes ordinary address-backed
   records and aggregate assignment values whose fully evaluated RHS snapshot
   is owned by minic_core_lower_internal_record_materialized_address().  The source pointer keeps
   its qualifiers so volatile aggregate reads remain explicit at the IR boundary. */
MinicCoreLowerStatus minic_core_lower_record_load_object(MinicCoreLowerContext *context,
                                                     MinicExpressionId expression_id,
                                                     MinicCoreObjectId *object_id) {
    const MinicExpression *expression;
    MinicCoreInstruction instruction;
    MinicCoreLowerStatus status;
    MinicCoreValueId source_address;
    MinicType expression_type;
    MinicType source_pointee;
    MinicType source_type;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || object_id == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL || !minic_type_is_record(expression->type) ||
        !minic_type_unqualified(expression->type, &expression_type) ||
        !minic_type_is_record(expression_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    status = minic_core_lower_internal_record_materialized_address(context, expression_id, &source_address);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    if (source_address >= context->function->value_count ||
        !minic_type_pointee(context->function->values[source_address].type, &source_pointee) ||
        !minic_type_unqualified(source_pointee, &source_type) ||
        !minic_type_equal(source_type, expression_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    if (!minic_core_function_add_object(
            context->function, expression->span, expression_type, object_id)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_RECORD_LOAD;
    instruction.span = expression->span;
    instruction.type = expression_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.record_load.source_address = source_address;
    instruction.value.record_load.destination_object = *object_id;
    instruction.value.record_load.is_volatile = minic_type_is_volatile(source_pointee);
    return minic_core_function_append_effect_instruction(
               context->function, context->block_id, &instruction)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

MinicCoreLowerStatus minic_core_lower_record_copy_statement(MinicCoreLowerContext *context,
                                                        const MinicStatement *statement) {
    const MinicExpression *source;
    const MinicExpression *target;
    const MinicRecord *record;
    MinicCoreInstruction instruction;
    MinicCoreLowerStatus status;
    MinicCoreValueId destination_address;
    MinicCoreValueId source_address;
    MinicType source_type;
    MinicType target_type;
    bool record_call;
    bool record_assignment_value;
    bool record_conditional_value;
    bool record_va_arg_value;
    bool record_statement_value;
    bool record_member_value;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || statement == NULL ||
        (statement->kind != MINIC_STATEMENT_RECORD_COPY &&
         statement->kind != MINIC_STATEMENT_RECORD_INITIALIZE)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    target = minic_c0_program_expression(context->body->program, statement->target_expression);
    source = minic_c0_program_expression(context->body->program, statement->expression);
    record_call = source != NULL && source->kind == MINIC_EXPRESSION_CALL;
    record_assignment_value =
        source != NULL && source->kind == MINIC_EXPRESSION_ASSIGNMENT;
    record_conditional_value =
        source != NULL && source->kind == MINIC_EXPRESSION_CONDITIONAL;
    record_va_arg_value =
        source != NULL && source->kind == MINIC_EXPRESSION_BUILTIN_VA_ARG;
    record_statement_value =
        source != NULL && source->kind == MINIC_EXPRESSION_STATEMENT;
    record_member_value =
        source != NULL && source->kind == MINIC_EXPRESSION_MEMBER &&
        source->value_category == MINIC_VALUE_RVALUE &&
        minic_type_is_record(source->type);
    if (target == NULL || source == NULL || target->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_is_record(target->type) || !minic_type_is_record(source->type) ||
        target->type.record_id != source->type.record_id ||
        !minic_type_unqualified(target->type, &target_type) ||
        !minic_type_unqualified(source->type, &source_type) ||
        !minic_type_equal(target_type, source_type) || !minic_type_is_record(target_type) ||
        (statement->kind == MINIC_STATEMENT_RECORD_COPY && minic_type_is_const(target->type)) ||
        (!record_call && !record_assignment_value && !record_conditional_value &&
         !record_va_arg_value && !record_statement_value && !record_member_value &&
         (!minic_c0_record_value_is_copy_source(context->body->program, statement->expression) ||
          !minic_c0_record_value_is_address_backed(
              context->body->program, statement->expression)))) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    record = minic_c0_program_record(context->body->program, target_type.record_id);
    if (record == NULL || !record->is_complete) {
        return MINIC_CORE_LOWER_ERROR;
    }
    if (record_call) {
        MinicCoreObjectId source_object;
        MinicType pointer_type;

        status = source->value.call.function_id != MINIC_FUNCTION_INVALID
            ? minic_core_lower_direct_record_call_object(context, source, &source_object)
            : minic_core_lower_indirect_record_call_object(context, source, &source_object);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (source_object >= context->function->object_count ||
            !minic_type_equal(context->function->objects[source_object].type, source_type) ||
            !minic_type_pointer_to(source_type, &pointer_type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
        instruction.span = source->span;
        instruction.type = pointer_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.object_id = source_object;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &source_address)) {
            return MINIC_CORE_LOWER_ERROR;
        }
    } else if (record_conditional_value || record_va_arg_value ||
               record_statement_value || record_member_value) {
        /* Conditional records and record-valued va_arg are materialized
           aggregate producers, not pre-existing address-backed objects. */
        status = minic_core_lower_internal_record_materialized_address(
            context, statement->expression, &source_address);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
    } else {
        status = lower_record_value_address(context, statement->expression, &source_address);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
    }
    status = lower_address(context, statement->target_expression, &destination_address);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_RECORD_COPY;
    instruction.span = statement->span;
    instruction.type = target_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.record_copy.destination_address = destination_address;
    instruction.value.record_copy.source_address = source_address;
    return minic_core_function_append_effect_instruction(
               context->function, context->block_id, &instruction)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

/* Record-rvalue member projection: record ABI/CFG materialization and field
 * address calculation are one source-to-Core aggregate family. A selected
 * record-valued conditional or compound literal has the same address-backed
 * scalar field semantics as a direct record-returning call. Do not claim
 * ordinary scalar member lvalues or bit-fields. */
MinicCoreLowerStatus minic_core_lower_try_direct_record_member(
    MinicCoreLowerContext *context, const MinicExpression *expression,
    MinicCoreValueId *value_id, bool *handled) {
    if (handled == NULL || context == NULL || expression == NULL || value_id == NULL ||
        context->body == NULL || context->body->program == NULL ||
        context->function == NULL)
        return MINIC_CORE_LOWER_ERROR;
    *handled = false;
    /* A scalar member projected from a materialized record rvalue must use
       the record materialization owner. In particular, lowering the `?:`
       arms before the member access guarantees exactly one arm's initializer
       effects, then selects its field from the merged private record object. */
    if (expression->kind == MINIC_EXPRESSION_MEMBER &&
        core_memory_scalar_type(expression->type)) {
        const MinicExpression *base;
        const MinicRecord *record;
        const MinicRecordField *field;
        MinicCoreValueId base_address;
        MinicCoreValueId field_address;
        MinicCoreLowerStatus status;
        MinicType pointer_type;
        MinicType value_type;

        base = minic_c0_program_expression(context->body->program, expression->value.member.base);
        record = minic_c0_program_record(context->body->program, expression->value.member.record_id);
        field = minic_c0_record_field(record, expression->value.member.field_index);
        if (base != NULL &&
            (base->kind == MINIC_EXPRESSION_CALL ||
             base->kind == MINIC_EXPRESSION_CONDITIONAL ||
             base->kind == MINIC_EXPRESSION_COMPOUND_LITERAL ||
             base->kind == MINIC_EXPRESSION_CAST ||
             base->kind == MINIC_EXPRESSION_STATEMENT ||
             base->kind == MINIC_EXPRESSION_MEMBER) &&
            minic_type_is_record(base->type) &&
            base->type.record_id == expression->value.member.record_id &&
            record != NULL && field != NULL && !field->is_bit_field &&
            minic_type_unqualified(expression->type, &value_type) &&
            core_memory_scalar_type(value_type)) {
            *handled = true;
            status = minic_core_lower_internal_record_materialized_address(
                context, expression->value.member.base, &base_address);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            if (!minic_type_pointer_to(base->type, &pointer_type)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            if (base_address >= context->function->value_count ||
                !minic_type_equal(context->function->values[base_address].type,
                                  pointer_type))
                return MINIC_CORE_LOWER_ERROR;
            status = minic_core_lower_internal_append_field_address(context,
                                          expression->span,
                                          base_address,
                                          expression->value.member.record_id,
                                          expression->value.member.field_index,
                                          expression->type,
                                          &field_address);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            return minic_core_lower_internal_emit_scalar_load(
                context, expression->span, value_type, field_address,
                minic_type_is_volatile(expression->type), value_id);
        }
    }
    return MINIC_CORE_LOWER_OK;
}

