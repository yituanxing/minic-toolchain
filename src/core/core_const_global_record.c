#include "core/core_const_global_record.h"
#include "core/cfg_constant_calls.h"
#include "core/local_facts.h"
#include "core/opt_pure_statement.h"
#include "frontend/const_eval.h"
#include <stdint.h>
#include <stdlib.h>

typedef struct MinicCoreConstGlobalArrayElement {
    MinicGlobalObjectId object_id;
    int64_t index;
} MinicCoreConstGlobalArrayElement;

static bool core_record_integer_truth(void *opaque_context,
                                      MinicExpressionId expression_id,
                                      bool *is_true) {
    const MinicCoreLowerContext *context =
        (const MinicCoreLowerContext *)opaque_context;
    MinicConstValue value;
    bool is_zero;
    if (!minic_core_cfg_pure_call_argument(context, expression_id, 0U) ||
        !core_const_eval_integer_with_locals(context, expression_id, &value) ||
        !minic_const_value_is_zero(context->body->program, context->target,
                                   &value, &is_zero)) {
        return false;
    }
    *is_true = !is_zero;
    return true;
}

/* Resolve only a fixed-index address of a TU-local global array. A bounded
 * pure inline getter may lead to that address through a selected immediate
 * RETURN; arbitrary calls, effectful prefixes, and runtime predicates remain
 * opaque. Exactly one global object and one array index leave the helper. */
static bool core_record_pointer_source(
    const MinicCoreLowerContext *context, MinicExpressionId pointer_id,
    unsigned int depth, MinicCoreConstGlobalArrayElement *result) {
    const MinicC0Program *program;
    const MinicExpression *pointer;
    const MinicExpression *subscript;
    const MinicExpression *base;
    MinicConstValue index_value;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->target == NULL || result == NULL || depth > 8U) {
        return false;
    }
    program = context->body->program;
    pointer = minic_c0_program_expression(program, pointer_id);
    if (pointer == NULL) {
        return false;
    }
    if ((pointer->kind == MINIC_EXPRESSION_CAST ||
         pointer->kind == MINIC_EXPRESSION_BITCAST ||
         pointer->kind == MINIC_EXPRESSION_CONVERSION) &&
        minic_type_is_pointer(pointer->type)) {
        const MinicExpression *source = minic_c0_program_expression(
            program, pointer->value.unary.operand);
        MinicType source_pointee;
        MinicType target_pointee;
        /* No reinterpret-cast arithmetic or guessed record byte offsets. */
        if (source == NULL || !minic_type_pointee(source->type, &source_pointee) ||
            !minic_type_pointee(pointer->type, &target_pointee) ||
            !minic_type_equal(source_pointee, target_pointee)) {
            return false;
        }
        return core_record_pointer_source(context, pointer->value.unary.operand,
                                          depth + 1U, result);
    }
    if (pointer->kind == MINIC_EXPRESSION_CALL) {
        const MinicFunction *callee;
        const MinicBlock *body;
        const MinicStatement *statement;
        MinicCoreLowerContext callee_context;
        MinicCoreLocalIntegerConstant *facts = NULL;
        MinicConstValue argument_value;
        size_t argument_index;
        size_t statement_index;
        bool condition_true;
        bool success = false;

        if (pointer->value.call.function_id == MINIC_FUNCTION_INVALID ||
            context->cfg_constant_call_depth >= 16U) {
            return false;
        }
        callee = minic_c0_program_function(program, pointer->value.call.function_id);
        if (callee == NULL || !callee->is_defined || !callee->is_internal ||
            !callee->is_inline || callee->body_block == MINIC_BLOCK_INVALID ||
            !minic_type_is_pointer(callee->return_type) ||
            callee->parameter_count != pointer->value.call.argument_count ||
            callee->local_count > SIZE_MAX / sizeof(*facts)) {
            return false;
        }
        body = minic_c0_program_block(program, callee->body_block);
        if (body == NULL || body->statement_count == 0U ||
            body->statement_count > 16U) {
            return false;
        }
        for (argument_index = 0U; argument_index < pointer->value.call.argument_count;
             ++argument_index) {
            if (!minic_type_is_integer(callee->parameter_types[argument_index]) ||
                !minic_core_cfg_pure_call_argument(
                    context, minic_c0_call_argument(pointer, argument_index), 0U) ||
                !core_const_eval_integer_with_locals(
                    context, minic_c0_call_argument(pointer, argument_index),
                    &argument_value)) {
                return false;
            }
        }
        if (callee->local_count != 0U) {
            facts = (MinicCoreLocalIntegerConstant *)calloc(callee->local_count,
                                                             sizeof(*facts));
            if (facts == NULL) {
                return false;
            }
        }
        callee_context = *context;
        callee_context.source_function = callee;
        callee_context.local_integer_constants = facts;
        callee_context.cfg_constant_call_depth++;
        for (argument_index = 0U; argument_index < pointer->value.call.argument_count;
             ++argument_index) {
            MinicLocalId local_id = callee->local_begin + argument_index;
            if (!core_const_eval_integer_with_locals(
                    context, minic_c0_call_argument(pointer, argument_index),
                    &argument_value)) {
                goto done;
            }
            core_local_constant_set(&callee_context, local_id, &argument_value);
        }
        for (statement_index = 0U; statement_index < body->statement_count;
             ++statement_index) {
            const MinicBlock *selected;
            const MinicStatement *return_statement;
            statement = minic_c0_program_statement(
                program, body->statements[statement_index]);
            if (statement == NULL) {
                goto done;
            }
            if (statement->kind == MINIC_STATEMENT_WHILE &&
                minic_opt_pure_guard_loop(program, callee_context.reentry_index,
                                          statement, &callee_context,
                                          core_record_integer_truth)) {
                continue;
            }
            if (statement->kind != MINIC_STATEMENT_IF ||
                statement->cleanup_context != statement->cleanup_stop_context ||
                statement->then_block == MINIC_BLOCK_INVALID ||
                statement->expression == MINIC_EXPRESSION_INVALID ||
                !core_record_integer_truth(&callee_context, statement->expression,
                                           &condition_true) || !condition_true) {
                goto done;
            }
            selected = minic_c0_program_block(program, statement->then_block);
            if (selected == NULL || selected->statement_count != 1U) {
                goto done;
            }
            return_statement = minic_c0_program_statement(program, selected->statements[0]);
            if (return_statement == NULL ||
                return_statement->kind != MINIC_STATEMENT_RETURN ||
                return_statement->expression == MINIC_EXPRESSION_INVALID ||
                return_statement->cleanup_context != return_statement->cleanup_stop_context ||
                !minic_core_cfg_pure_call_argument(
                    &callee_context, return_statement->expression, 0U)) {
                goto done;
            }
            success = core_record_pointer_source(
                &callee_context, return_statement->expression, depth + 1U, result);
            goto done;
        }
    done:
        free(facts);
        return success;
    }
    if (pointer->kind == MINIC_EXPRESSION_ADDRESS_OF) {
        pointer = minic_c0_program_expression(program, pointer->value.unary.operand);
    }
    if (pointer == NULL || pointer->kind != MINIC_EXPRESSION_SUBSCRIPT ||
        !minic_core_cfg_pure_call_argument(context, pointer_id, 0U)) {
        return false;
    }
    subscript = pointer;
    base = minic_c0_program_expression(program, subscript->value.subscript.base);
    if (base == NULL || base->kind != MINIC_EXPRESSION_GLOBAL_OBJECT ||
        !core_const_eval_integer_with_locals(
            context, subscript->value.subscript.index, &index_value) ||
        !minic_const_value_as_int64(program, context->target, &index_value,
                                    &result->index)) {
        return false;
    }
    result->object_id = base->value.global_object_id;
    return true;
}

bool minic_core_const_global_record_scalar(
    const MinicCoreLowerContext *context, MinicExpressionId expression_id,
    MinicConstValue *value) {
    const MinicC0Program *program;
    const MinicExpression *read;
    const MinicExpression *member;
    const MinicGlobalObject *object;
    const MinicArrayType *array;
    const MinicRecord *record;
    const MinicRecordField *field;
    MinicCoreConstGlobalArrayElement element;
    MinicConstValue scalar;
    MinicType object_type;
    MinicType member_type;
    MinicType element_type;
    size_t record_slots;
    size_t field_slot;
    size_t total_slots;
    size_t scalar_slot;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->target == NULL || value == NULL) {
        return false;
    }
    program = context->body->program;
    read = minic_c0_program_expression(program, expression_id);
    if (read == NULL || !minic_type_is_integer(read->type)) {
        return false;
    }
    member = read->kind == MINIC_EXPRESSION_LVALUE_READ
                 ? minic_c0_program_expression(program, read->value.unary.operand)
                 : read;
    if (member == NULL || member->kind != MINIC_EXPRESSION_MEMBER ||
        minic_type_is_volatile(member->type)) {
        return false;
    }
    if (!core_record_pointer_source(context, member->value.member.base, 0U, &element)) {
        return false;
    }
    object = minic_c0_program_global_object(program, element.object_id);
    if (object == NULL || !object->is_internal || !object->is_read_only ||
        object->is_extern || object->is_thread_local ||
        object->relocation_count != 0U || object->union_selection_count != 0U ||
        !minic_type_unqualified(object->type, &object_type) ||
        !minic_type_is_array(object_type)) {
        return false;
    }
    array = minic_c0_program_array_type(program, object_type.array_type_id);
    if (array == NULL || array->is_zero_length || array->element_count == 0U ||
        !minic_type_unqualified(array->element_type, &element_type) ||
        !minic_type_is_record(element_type) ||
        minic_type_is_volatile(array->element_type) ||
        element_type.record_id != member->value.member.record_id) {
        return false;
    }
    record = minic_c0_program_record(program, element_type.record_id);
    field = minic_c0_record_field(record, member->value.member.field_index);
    if (record == NULL || !record->is_complete || record->is_union || field == NULL ||
        field->is_bit_field || field->is_flexible_array || field->is_zero_length_array ||
        field->element_count != 1U || !minic_type_is_integer(field->type) ||
        minic_type_is_volatile(field->type) ||
        !minic_type_unqualified(field->type, &member_type) ||
        !minic_c0_type_initializer_slot_count(program, element_type, &record_slots) ||
        !minic_c0_global_record_field_initializer_slot(
            program, record, member->value.member.field_index, &field_slot) ||
        record_slots == 0U || field_slot >= record_slots ||
        array->element_count > SIZE_MAX / record_slots) {
        return false;
    }
    total_slots = array->element_count * record_slots;
    if ((!object->is_zero_initialized && object->initializer_count != total_slots) ||
        (object->is_zero_initialized && object->initializer_count != 0U) ||
        element.index < 0 || (uint64_t)element.index >= array->element_count) {
        return false;
    }
    scalar_slot = (size_t)element.index * record_slots + field_slot;
    scalar.type = member_type;
    scalar.bits = object->is_zero_initialized ? 0U : object->initializer_values[scalar_slot];
    return minic_const_value_convert_integer(
        program, context->target, &scalar, read->type, value);
}
