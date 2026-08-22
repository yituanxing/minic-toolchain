#include "core/core_lower.h"

#include "frontend/const_eval.h"
#include "frontend/expression_semantics.h"
#include <stdlib.h>
#include <string.h>

typedef struct MinicCoreLowerContext {
    const MinicFunctionBodyView *body;
    const MinicFunction *source_function;
    const MinicTargetInfo *target;
    MinicCoreFunction *function;
    MinicCoreBlockId block_id;
    MinicCoreObjectId *local_objects;
} MinicCoreLowerContext;

static MinicCoreLowerStatus lower_expression(MinicCoreLowerContext *context,
                                             MinicExpressionId expression_id,
                                             MinicCoreValueId *value_id);
static MinicCoreLowerStatus
lower_block(MinicCoreLowerContext *context, const MinicBlock *source_block, bool *terminated);
static MinicCoreLowerStatus set_branch(MinicCoreLowerContext *context,
                                       MinicCoreBlockId block_id,
                                       MinicSourceSpan span,
                                       MinicCoreBlockId target);
static MinicCoreLowerStatus lower_condition_branch(MinicCoreLowerContext *context,
                                                   MinicExpressionId expression_id,
                                                   MinicSourceSpan span,
                                                   MinicCoreBlockId when_true,
                                                   MinicCoreBlockId when_false);
static MinicCoreLowerStatus lower_postfix_scalar_update(MinicCoreLowerContext *context,
                                                        const MinicExpression *expression,
                                                        MinicCoreValueId *value_id);
static MinicCoreLowerStatus spill_scalar_value(MinicCoreLowerContext *context,
                                               MinicSourceSpan span,
                                               MinicType type,
                                               MinicCoreValueId value_id,
                                               MinicCoreObjectId *object_id);
static MinicCoreLowerStatus reload_scalar_value(MinicCoreLowerContext *context,
                                                MinicSourceSpan span,
                                                MinicType type,
                                                MinicCoreObjectId object_id,
                                                MinicCoreValueId *value_id);

static bool core_memory_scalar_type(MinicType type) {
    return minic_type_is_integer(type) || minic_type_is_pointer(type);
}

static bool core_scalar_expression_value_type(const MinicFunctionBodyView *body,
                                              const MinicExpression *expression,
                                              MinicType *value_type) {
    const MinicExpression *statement_result;

    if (body == NULL || body->program == NULL || expression == NULL || value_type == NULL ||
        !core_memory_scalar_type(expression->type)) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_STATEMENT) {
        if (expression->value.statement_expression.result == MINIC_EXPRESSION_INVALID) {
            return false;
        }
        statement_result = minic_c0_program_expression(
            body->program, expression->value.statement_expression.result);
        return statement_result != NULL &&
               core_scalar_expression_value_type(body, statement_result, value_type);
    }
    if (expression->value_category == MINIC_VALUE_LVALUE) {
        return minic_type_unqualified(expression->type, value_type);
    }
    if (expression->value_category != MINIC_VALUE_RVALUE) {
        return false;
    }
    *value_type = expression->type;
    return true;
}

static MinicCoreLowerStatus lower_local_object(MinicCoreLowerContext *context,
                                               MinicLocalId local_id,
                                               MinicCoreObjectId *object_id) {
    const MinicLocal *local;
    size_t local_index;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->source_function == NULL || context->function == NULL || object_id == NULL ||
        local_id < context->source_function->local_begin) {
        return MINIC_CORE_LOWER_ERROR;
    }
    local_index = local_id - context->source_function->local_begin;
    if (local_index >= context->source_function->local_count || context->local_objects == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    if (context->local_objects[local_index] != MINIC_CORE_OBJECT_INVALID) {
        *object_id = context->local_objects[local_index];
        return MINIC_CORE_LOWER_OK;
    }
    local = minic_c0_program_local(context->body->program, local_id);
    if (local == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    if (local->is_array || local->is_register_storage || !core_memory_scalar_type(local->type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    if (!minic_core_function_add_object(
            context->function, local->name_span, local->type, object_id)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    context->local_objects[local_index] = *object_id;
    return MINIC_CORE_LOWER_OK;
}

static MinicCoreLowerStatus lower_parameter_ingress(MinicCoreLowerContext *context) {
    size_t parameter_index;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->source_function == NULL || context->function == NULL ||
        context->source_function->parameter_count > context->source_function->local_count) {
        return MINIC_CORE_LOWER_ERROR;
    }
    for (parameter_index = 0U; parameter_index < context->source_function->parameter_count;
         ++parameter_index) {
        const MinicLocal *parameter;
        MinicCoreInstruction instruction;
        MinicCoreObjectId object_id;
        MinicCoreValueId address_id;
        MinicCoreValueId parameter_value;
        MinicCoreLowerStatus status;
        MinicLocalId local_id;
        MinicType parameter_value_type;
        MinicType pointer_type;

        local_id = context->source_function->local_begin + parameter_index;
        parameter = minic_c0_program_local(context->body->program, local_id);
        if (parameter == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (!core_memory_scalar_type(parameter->type) || minic_type_is_volatile(parameter->type) ||
            parameter->is_array || parameter->is_register_storage) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        if (!minic_type_unqualified(parameter->type, &parameter_value_type) ||
            !core_memory_scalar_type(parameter_value_type) ||
            minic_type_is_const(parameter_value_type) ||
            minic_type_is_volatile(parameter_value_type) ||
            !minic_type_equal(parameter_value_type,
                              context->source_function->parameter_types[parameter_index])) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = lower_local_object(context, local_id, &object_id);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }

        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_PARAMETER;
        instruction.span = parameter->name_span;
        instruction.type = parameter_value_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.parameter_index = parameter_index;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &parameter_value)) {
            return MINIC_CORE_LOWER_ERROR;
        }

        if (!minic_type_pointer_to(parameter->type, &pointer_type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
        instruction.span = parameter->name_span;
        instruction.type = pointer_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.object_id = object_id;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &address_id)) {
            return MINIC_CORE_LOWER_ERROR;
        }

        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
        instruction.span = parameter->name_span;
        instruction.type = minic_type_void();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.store.address = address_id;
        instruction.value.store.stored_value = parameter_value;
        instruction.value.store.is_volatile = false;
        if (!minic_core_function_append_effect_instruction(
                context->function, context->block_id, &instruction)) {
            return MINIC_CORE_LOWER_ERROR;
        }
    }
    return MINIC_CORE_LOWER_OK;
}

static MinicCoreLowerStatus append_field_address(MinicCoreLowerContext *context,
                                                 MinicSourceSpan span,
                                                 MinicCoreValueId base_id,
                                                 MinicRecordId record_id,
                                                 size_t field_index,
                                                 MinicType field_type,
                                                 MinicCoreValueId *address_id) {
    MinicCoreInstruction instruction;
    MinicType base_pointee;

    if (context == NULL || context->function == NULL || address_id == NULL ||
        base_id >= context->function->value_count ||
        !minic_type_pointee(context->function->values[base_id].type, &base_pointee) ||
        !minic_type_is_record(base_pointee) || base_pointee.record_id != record_id ||
        record_id == MINIC_RECORD_INVALID) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_FIELD_ADDRESS;
    instruction.span = span;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.field_address.base = base_id;
    instruction.value.field_address.record_id = record_id;
    instruction.value.field_address.field_index = field_index;
    if (!minic_type_pointer_to(field_type, &instruction.type)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    return minic_core_function_append_value_instruction(
               context->function, context->block_id, &instruction, address_id)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

static MinicCoreLowerStatus lower_address(MinicCoreLowerContext *context,
                                          MinicExpressionId expression_id,
                                          MinicCoreValueId *address_id) {
    const MinicExpression *expression;
    MinicCoreInstruction instruction;
    MinicCoreObjectId object_id;
    MinicCoreLowerStatus status;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || address_id == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    if (expression->value_category != MINIC_VALUE_LVALUE) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    if (expression->kind == MINIC_EXPRESSION_LOCAL) {
        status = lower_local_object(context, expression->value.local_id, &object_id);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
        instruction.span = expression->span;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.object_id = object_id;
        if (!minic_type_pointer_to(expression->type, &instruction.type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, address_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_GLOBAL_OBJECT) {
        const MinicGlobalObject *global;
        MinicCoreGlobalId global_id;

        global = minic_c0_program_global_object(context->body->program,
                                                expression->value.global_object_id);
        if (global == NULL || global->name == NULL || global->name_length == 0U) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (!minic_type_equal(global->type, expression->type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (!core_memory_scalar_type(global->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        if (!minic_core_function_add_global(
                context->function, global->name, global->name_length, global->type, &global_id)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS;
        instruction.span = expression->span;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.global_id = global_id;
        if (!minic_type_pointer_to(expression->type, &instruction.type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, address_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_DEREFERENCE) {
        MinicCoreValueId pointer_id;
        MinicType expected_pointer;

        if (!minic_type_pointer_to(expression->type, &expected_pointer)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = lower_expression(context, expression->value.unary.operand, &pointer_id);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (pointer_id >= context->function->value_count ||
            !minic_type_equal(context->function->values[pointer_id].type, expected_pointer)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        *address_id = pointer_id;
        return MINIC_CORE_LOWER_OK;
    }
    if (expression->kind == MINIC_EXPRESSION_SUBSCRIPT) {
        const MinicExpression *base;
        const MinicExpression *index;
        MinicCoreInstruction offset_instruction;
        MinicCoreObjectId base_object;
        MinicCoreValueId base_value;
        MinicCoreValueId index_value;
        MinicCoreLowerStatus subscript_status;
        MinicType element_type;
        size_t element_size;

        base =
            minic_c0_program_expression(context->body->program, expression->value.subscript.base);
        index =
            minic_c0_program_expression(context->body->program, expression->value.subscript.index);
        if (base == NULL || index == NULL || !minic_type_is_pointer(base->type) ||
            !minic_type_is_integer(index->type) || !minic_type_pointee(base->type, &element_type) ||
            !minic_type_equal(element_type, expression->type) ||
            !minic_c0_pointer_arithmetic_element_size(
                context->body->program, minic_default_data_layout(), base->type, &element_size)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        subscript_status = lower_expression(context, expression->value.subscript.base, &base_value);
        if (subscript_status != MINIC_CORE_LOWER_OK) {
            return subscript_status;
        }
        if (base_value >= context->function->value_count ||
            !minic_type_equal(context->function->values[base_value].type, base->type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        subscript_status =
            spill_scalar_value(context, base->span, base->type, base_value, &base_object);
        if (subscript_status != MINIC_CORE_LOWER_OK) {
            return subscript_status;
        }
        subscript_status =
            lower_expression(context, expression->value.subscript.index, &index_value);
        if (subscript_status != MINIC_CORE_LOWER_OK) {
            return subscript_status;
        }
        if (index_value >= context->function->value_count ||
            !minic_type_equal(context->function->values[index_value].type, index->type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        subscript_status =
            reload_scalar_value(context, base->span, base->type, base_object, &base_value);
        if (subscript_status != MINIC_CORE_LOWER_OK) {
            return subscript_status;
        }

        (void)memset(&offset_instruction, 0, sizeof(offset_instruction));
        offset_instruction.kind = MINIC_CORE_INSTRUCTION_POINTER_OFFSET;
        offset_instruction.span = expression->span;
        offset_instruction.type = base->type;
        offset_instruction.result = MINIC_CORE_VALUE_INVALID;
        offset_instruction.value.pointer_offset.base = base_value;
        offset_instruction.value.pointer_offset.index = index_value;
        offset_instruction.value.pointer_offset.element_size = element_size;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &offset_instruction, address_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_MEMBER) {
        const MinicExpression *base;
        const MinicRecord *record;
        const MinicRecordField *field;
        MinicCoreValueId base_id;
        MinicType record_type;

        base = minic_c0_program_expression(context->body->program, expression->value.member.base);
        record =
            minic_c0_program_record(context->body->program, expression->value.member.record_id);
        field = minic_c0_record_field(record, expression->value.member.field_index);
        if (base == NULL || record == NULL || field == NULL || field->is_bit_field ||
            !minic_type_pointee(base->type, &record_type) || !minic_type_is_record(record_type) ||
            record_type.record_id != expression->value.member.record_id) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_expression(context, expression->value.member.base, &base_id);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (base_id >= context->function->value_count ||
            !minic_type_equal(context->function->values[base_id].type, base->type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        return append_field_address(context,
                                    expression->span,
                                    base_id,
                                    expression->value.member.record_id,
                                    expression->value.member.field_index,
                                    expression->type,
                                    address_id);
    }
    return MINIC_CORE_LOWER_UNSUPPORTED;
}

static MinicCoreLowerStatus append_integer_conversion(MinicCoreLowerContext *context,
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

static MinicCoreLowerStatus append_scalar_bitcast(MinicCoreLowerContext *context,
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

static MinicCoreLowerStatus spill_scalar_value(MinicCoreLowerContext *context,
                                               MinicSourceSpan span,
                                               MinicType type,
                                               MinicCoreValueId value_id,
                                               MinicCoreObjectId *object_id) {
    MinicCoreInstruction instruction;
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

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
    instruction.span = span;
    instruction.type = pointer_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.object_id = *object_id;
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &instruction, &address_id)) {
        return MINIC_CORE_LOWER_ERROR;
    }

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
    instruction.span = span;
    instruction.type = minic_type_void();
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.store.address = address_id;
    instruction.value.store.stored_value = value_id;
    instruction.value.store.is_volatile = false;
    return minic_core_function_append_effect_instruction(
               context->function, context->block_id, &instruction)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

static MinicCoreLowerStatus store_scalar_value(MinicCoreLowerContext *context,
                                               MinicSourceSpan span,
                                               MinicType type,
                                               MinicCoreObjectId object_id,
                                               MinicCoreValueId value_id) {
    MinicCoreInstruction instruction;
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

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
    instruction.span = span;
    instruction.type = pointer_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.object_id = object_id;
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &instruction, &address_id)) {
        return MINIC_CORE_LOWER_ERROR;
    }

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
    instruction.span = span;
    instruction.type = minic_type_void();
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.store.address = address_id;
    instruction.value.store.stored_value = value_id;
    instruction.value.store.is_volatile = false;
    return minic_core_function_append_effect_instruction(
               context->function, context->block_id, &instruction)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

static MinicCoreLowerStatus reload_scalar_value(MinicCoreLowerContext *context,
                                                MinicSourceSpan span,
                                                MinicType type,
                                                MinicCoreObjectId object_id,
                                                MinicCoreValueId *value_id) {
    MinicCoreInstruction instruction;
    MinicCoreValueId address_id;
    MinicType pointer_type;

    if (context == NULL || context->function == NULL || value_id == NULL ||
        !core_memory_scalar_type(type) || minic_type_is_const(type) ||
        minic_type_is_volatile(type) || object_id >= context->function->object_count ||
        !minic_type_equal(context->function->objects[object_id].type, type) ||
        !minic_type_pointer_to(type, &pointer_type)) {
        return MINIC_CORE_LOWER_ERROR;
    }

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
    instruction.span = span;
    instruction.type = pointer_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.object_id = object_id;
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &instruction, &address_id)) {
        return MINIC_CORE_LOWER_ERROR;
    }

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_LOAD;
    instruction.span = span;
    instruction.type = type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.load.address = address_id;
    instruction.value.load.is_volatile = false;
    return minic_core_function_append_value_instruction(
               context->function, context->block_id, &instruction, value_id)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

static MinicCoreLowerStatus lower_scalar_equality_operands(MinicCoreLowerContext *context,
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

    pointer_comparison = false;
    if (minic_type_is_integer(left_type) && minic_type_is_integer(right_type)) {
        if (!minic_type_equal(left_type, right_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        comparison_type = left_type;
    } else if (minic_type_is_pointer(left_type) && minic_type_is_pointer(right_type)) {
        if (!minic_type_pointer_equality_compatible(left_type, right_type) ||
            !minic_type_conditional_pointer_common(left_type, right_type, &comparison_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
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
        return status;
    }
    if (left_source >= context->function->value_count ||
        !minic_type_equal(context->function->values[left_source].type, left_type)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    if (pointer_comparison) {
        status = append_scalar_bitcast(
            context, left_expression->span, comparison_type, left_source, &left_normalized);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
    } else {
        left_normalized = left_source;
    }
    status = spill_scalar_value(
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
    if (pointer_comparison) {
        status = append_scalar_bitcast(
            context, right_expression->span, comparison_type, right_source, &right_normalized);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
    } else {
        right_normalized = right_source;
    }
    status = reload_scalar_value(
        context, left_expression->span, comparison_type, left_object, left_value);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    *right_value = right_normalized;
    return MINIC_CORE_LOWER_OK;
}

static MinicCoreLowerStatus lower_integer_binary_operands(MinicCoreLowerContext *context,
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
        return status;
    }
    status = append_integer_conversion(
        context, left_expression->span, result_type, left_source, &left_normalized);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    status = spill_scalar_value(
        context, left_expression->span, result_type, left_normalized, &left_object);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }

    status = lower_expression(context, right_id, &right_source);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    status = append_integer_conversion(
        context, right_expression->span, result_type, right_source, &right_normalized);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    status =
        reload_scalar_value(context, left_expression->span, result_type, left_object, left_value);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    *right_value = right_normalized;
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
    return append_integer_conversion(
        context, expression->span, result_type, source_value, value_id);
}

static MinicCoreLowerStatus lower_scalar_assignment_value(MinicCoreLowerContext *context,
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
    if (expression == NULL ||
        !minic_c0_assignment_compatible(context->body->program, target_type, expression_id)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    if (minic_type_is_integer(target_type) && minic_type_is_integer(expression->type)) {
        return lower_integer_assignment_value(context, target_type, expression_id, value_id);
    }

    status = lower_expression(context, expression_id, &source_value);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    if (source_value >= context->function->value_count) {
        return MINIC_CORE_LOWER_ERROR;
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
    return append_integer_conversion(context, expression->span, target_type, truth_value, value_id);
}

static MinicCoreLowerStatus lower_direct_call(MinicCoreLowerContext *context,
                                              const MinicExpression *expression,
                                              MinicCoreValueId *value_id) {
    const MinicFunction *callee;
    const char *callee_name;
    size_t callee_name_length;
    MinicCoreCalleeId callee_id;
    MinicCoreInstruction instruction;
    MinicCoreValueId *arguments;
    MinicCoreLowerStatus status;
    size_t argument_begin;
    size_t argument_index;
    bool returns_void;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || expression == NULL || value_id == NULL ||
        expression->kind != MINIC_EXPRESSION_CALL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    if (expression->value.call.function_id == MINIC_FUNCTION_INVALID) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    callee = minic_c0_program_function(context->body->program, expression->value.call.function_id);
    if (callee == NULL || callee->name == NULL || callee->name_length == 0U) {
        return MINIC_CORE_LOWER_ERROR;
    }
    callee_name = callee->assembler_name != NULL ? callee->assembler_name : callee->name;
    callee_name_length =
        callee->assembler_name != NULL ? callee->assembler_name_length : callee->name_length;
    if (callee_name == NULL || callee_name_length == 0U) {
        return MINIC_CORE_LOWER_ERROR;
    }
    returns_void = minic_type_is_void(callee->return_type);
    if (callee->is_variadic || expression->value.call.argument_count != callee->parameter_count ||
        (!returns_void && !core_memory_scalar_type(callee->return_type))) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    for (argument_index = 0U; argument_index < callee->parameter_count; ++argument_index) {
        if (!core_memory_scalar_type(callee->parameter_types[argument_index])) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
    }
    arguments = callee->parameter_count == 0U
                    ? NULL
                    : (MinicCoreValueId *)malloc(callee->parameter_count * sizeof(*arguments));
    if (callee->parameter_count != 0U && arguments == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    for (argument_index = 0U; argument_index < callee->parameter_count; ++argument_index) {
        status = lower_scalar_assignment_value(context,
                                               callee->parameter_types[argument_index],
                                               expression->value.call.arguments[argument_index],
                                               &arguments[argument_index]);
        if (status != MINIC_CORE_LOWER_OK) {
            free(arguments);
            return status;
        }
        if (arguments[argument_index] >= context->function->value_count ||
            !minic_type_equal(context->function->values[arguments[argument_index]].type,
                              callee->parameter_types[argument_index])) {
            free(arguments);
            return MINIC_CORE_LOWER_ERROR;
        }
    }
    if (!minic_core_function_add_callee(context->function,
                                        callee_name,
                                        callee_name_length,
                                        callee->return_type,
                                        callee->parameter_types,
                                        callee->parameter_count,
                                        &callee_id) ||
        !minic_core_function_append_call_arguments(
            context->function, arguments, callee->parameter_count, &argument_begin)) {
        free(arguments);
        return MINIC_CORE_LOWER_ERROR;
    }
    free(arguments);
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_CALL;
    instruction.span = expression->span;
    instruction.type = callee->return_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.call.callee_id = callee_id;
    instruction.value.call.argument_begin = argument_begin;
    instruction.value.call.argument_count = callee->parameter_count;
    if (returns_void) {
        *value_id = MINIC_CORE_VALUE_INVALID;
        return minic_core_function_append_effect_instruction(
                   context->function, context->block_id, &instruction)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    return minic_core_function_append_value_instruction(
               context->function, context->block_id, &instruction, value_id)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

static MinicCoreLowerStatus lower_expression(MinicCoreLowerContext *context,
                                             MinicExpressionId expression_id,
                                             MinicCoreValueId *value_id) {
    const MinicExpression *expression;
    MinicCoreInstruction instruction;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || value_id == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    if (expression->value_category == MINIC_VALUE_LVALUE &&
        core_memory_scalar_type(expression->type)) {
        MinicCoreValueId address_id;
        MinicCoreLowerStatus status;

        status = lower_address(context, expression_id, &address_id);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_LOAD;
        instruction.span = expression->span;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.load.address = address_id;
        instruction.value.load.is_volatile = minic_type_is_volatile(expression->type);
        if (!minic_type_unqualified(expression->type, &instruction.type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->value_category != MINIC_VALUE_RVALUE) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    if (expression->kind == MINIC_EXPRESSION_DISCARD) {
        const MinicExpression *operand;
        MinicCoreValueId discarded_value;
        MinicCoreLowerStatus status;

        if (!minic_type_is_void(expression->type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        operand =
            minic_c0_program_expression(context->body->program, expression->value.unary.operand);
        if (operand == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = lower_expression(context, expression->value.unary.operand, &discarded_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (minic_type_is_void(operand->type)) {
            if (discarded_value != MINIC_CORE_VALUE_INVALID) {
                return MINIC_CORE_LOWER_ERROR;
            }
        } else if (discarded_value == MINIC_CORE_VALUE_INVALID ||
                   discarded_value >= context->function->value_count) {
            return MINIC_CORE_LOWER_ERROR;
        }
        *value_id = MINIC_CORE_VALUE_INVALID;
        return MINIC_CORE_LOWER_OK;
    }
    if (expression->kind == MINIC_EXPRESSION_STATEMENT) {
        const MinicBlock *statement_block;
        const MinicExpression *statement_result;
        MinicCoreValueId result_value;
        MinicCoreLowerStatus status;
        MinicType result_type;
        bool terminated;

        if (expression->value.statement_expression.result == MINIC_EXPRESSION_INVALID ||
            !core_scalar_expression_value_type(context->body, expression, &result_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        statement_block = minic_c0_program_block(context->body->program,
                                                 expression->value.statement_expression.block);
        statement_result = minic_c0_program_expression(
            context->body->program, expression->value.statement_expression.result);
        if (statement_block == NULL || statement_result == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = lower_block(context, statement_block, &terminated);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (terminated) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status =
            lower_expression(context, expression->value.statement_expression.result, &result_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (result_value >= context->function->value_count ||
            !minic_type_equal(context->function->values[result_value].type, result_type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        *value_id = result_value;
        return MINIC_CORE_LOWER_OK;
    }
    if (expression->kind == MINIC_EXPRESSION_CONDITIONAL) {
        const MinicExpression *false_expression;
        const MinicExpression *true_expression;
        MinicCoreBlockId false_block;
        MinicCoreBlockId merge_block;
        MinicCoreBlockId true_block;
        MinicCoreObjectId result_object;
        MinicCoreValueId arm_value;
        MinicCoreLowerStatus status;
        MinicType false_type;
        MinicType true_type;

        if (expression->value.conditional.uses_condition_value ||
            expression->value.conditional.when_true == MINIC_EXPRESSION_INVALID ||
            expression->value.conditional.when_false == MINIC_EXPRESSION_INVALID ||
            !minic_type_is_integer(expression->type) || minic_type_is_const(expression->type) ||
            minic_type_is_volatile(expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        true_expression = minic_c0_program_expression(context->body->program,
                                                      expression->value.conditional.when_true);
        false_expression = minic_c0_program_expression(context->body->program,
                                                       expression->value.conditional.when_false);
        if (true_expression == NULL || false_expression == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (!core_scalar_expression_value_type(context->body, true_expression, &true_type) ||
            !core_scalar_expression_value_type(context->body, false_expression, &false_type) ||
            !minic_type_equal(true_type, expression->type) ||
            !minic_type_equal(false_type, expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        if (!minic_core_function_add_object(
                context->function, expression->span, expression->type, &result_object) ||
            !minic_core_function_add_block(context->function, &true_block) ||
            !minic_core_function_add_block(context->function, &false_block) ||
            !minic_core_function_add_block(context->function, &merge_block)) {
            return MINIC_CORE_LOWER_ERROR;
        }

        status = lower_condition_branch(context,
                                        expression->value.conditional.condition,
                                        expression->span,
                                        true_block,
                                        false_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }

        context->block_id = true_block;
        status = lower_expression(context, expression->value.conditional.when_true, &arm_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = store_scalar_value(
            context, true_expression->span, expression->type, result_object, arm_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = set_branch(context, context->block_id, expression->span, merge_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }

        context->block_id = false_block;
        status = lower_expression(context, expression->value.conditional.when_false, &arm_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = store_scalar_value(
            context, false_expression->span, expression->type, result_object, arm_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = set_branch(context, context->block_id, expression->span, merge_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }

        context->block_id = merge_block;
        return reload_scalar_value(
            context, expression->span, expression->type, result_object, value_id);
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_AND) {
        MinicCoreBlockId false_block;
        MinicCoreBlockId merge_block;
        MinicCoreBlockId true_block;
        MinicCoreObjectId result_object;
        MinicCoreValueId address_value;
        MinicCoreValueId constant_value;
        MinicCoreLowerStatus status;
        MinicType result_pointer_type;

        if (!minic_type_equal(expression->type, minic_type_int()) ||
            !minic_type_pointer_to(minic_type_int(), &result_pointer_type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (!minic_core_function_add_object(
                context->function, expression->span, minic_type_int(), &result_object) ||
            !minic_core_function_add_block(context->function, &true_block) ||
            !minic_core_function_add_block(context->function, &false_block) ||
            !minic_core_function_add_block(context->function, &merge_block)) {
            return MINIC_CORE_LOWER_ERROR;
        }

        status = lower_condition_branch(
            context, expression_id, expression->span, true_block, false_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }

        context->block_id = false_block;
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
        instruction.span = expression->span;
        instruction.type = minic_type_int();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.integer_value = 0;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &constant_value)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
        instruction.span = expression->span;
        instruction.type = result_pointer_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.object_id = result_object;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &address_value)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
        instruction.span = expression->span;
        instruction.type = minic_type_void();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.store.address = address_value;
        instruction.value.store.stored_value = constant_value;
        instruction.value.store.is_volatile = false;
        if (!minic_core_function_append_effect_instruction(
                context->function, context->block_id, &instruction)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = set_branch(context, context->block_id, expression->span, merge_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }

        context->block_id = true_block;
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
        instruction.span = expression->span;
        instruction.type = minic_type_int();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.integer_value = 1;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &constant_value)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
        instruction.span = expression->span;
        instruction.type = result_pointer_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.object_id = result_object;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &address_value)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
        instruction.span = expression->span;
        instruction.type = minic_type_void();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.store.address = address_value;
        instruction.value.store.stored_value = constant_value;
        instruction.value.store.is_volatile = false;
        if (!minic_core_function_append_effect_instruction(
                context->function, context->block_id, &instruction)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = set_branch(context, context->block_id, expression->span, merge_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }

        context->block_id = merge_block;
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
        instruction.span = expression->span;
        instruction.type = result_pointer_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.object_id = result_object;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &address_value)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_LOAD;
        instruction.span = expression->span;
        instruction.type = minic_type_int();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.load.address = address_value;
        instruction.value.load.is_volatile = false;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_ADDRESS_OF) {
        MinicCoreLowerStatus status;

        status = lower_address(context, expression->value.unary.operand, value_id);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (*value_id >= context->function->value_count ||
            !minic_type_equal(context->function->values[*value_id].type, expression->type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        return MINIC_CORE_LOWER_OK;
    }
    if (expression->kind == MINIC_EXPRESSION_CALL) {
        return lower_direct_call(context, expression, value_id);
    }
    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        (expression->value.unary.operator_kind == MINIC_UNARY_POST_INCREMENT ||
         expression->value.unary.operator_kind == MINIC_UNARY_POST_DECREMENT)) {
        return lower_postfix_scalar_update(context, expression, value_id);
    }
    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        expression->value.unary.operator_kind == MINIC_UNARY_NEGATE) {
        MinicCoreValueId operand_value;
        MinicCoreLowerStatus status;

        if (!minic_type_is_integer(expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_expression(context, expression->value.unary.operand, &operand_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (operand_value >= context->function->value_count ||
            !minic_type_equal(context->function->values[operand_value].type, expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_NEGATE;
        instruction.span = expression->span;
        instruction.type = expression->type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.operand = operand_value;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        expression->value.unary.operator_kind == MINIC_UNARY_BITWISE_NOT) {
        MinicCoreValueId operand_value;
        MinicCoreLowerStatus status;

        if (!minic_type_is_integer(expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_expression(context, expression->value.unary.operand, &operand_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (operand_value >= context->function->value_count ||
            !minic_type_equal(context->function->values[operand_value].type, expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT;
        instruction.span = expression->span;
        instruction.type = expression->type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.operand = operand_value;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        expression->value.unary.operator_kind == MINIC_UNARY_LOGICAL_NOT) {
        MinicCoreValueId operand_value;
        MinicCoreLowerStatus status;

        if (!minic_type_equal(expression->type, minic_type_int())) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = lower_expression(context, expression->value.unary.operand, &operand_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (operand_value >= context->function->value_count ||
            !core_memory_scalar_type(context->function->values[operand_value].type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO;
        instruction.span = expression->span;
        instruction.type = expression->type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.operand = operand_value;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.span = expression->span;
    instruction.type = expression->type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    if (expression->kind == MINIC_EXPRESSION_INTEGER) {
        if (!minic_type_is_integer(expression->type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
        instruction.value.integer_value = expression->value.integer_value;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_CONVERSION) {
        const MinicExpression *operand;
        MinicExpressionId operand_id;
        MinicCoreValueId operand_value;
        MinicCoreLowerStatus status;
        MinicType target_type;

        operand_id = expression->value.unary.operand;
        operand = minic_c0_program_expression(context->body->program, operand_id);
        if (operand == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (!minic_type_is_integer(expression->type) || !minic_type_is_integer(operand->type) ||
            !minic_type_unqualified(expression->type, &target_type) ||
            !minic_type_equal(target_type, expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_expression(context, operand_id, &operand_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        return append_integer_conversion(
            context, expression->span, target_type, operand_value, value_id);
    }
    if (expression->kind == MINIC_EXPRESSION_BITCAST) {
        const MinicExpression *operand;
        MinicCoreValueId operand_value;
        MinicCoreLowerStatus status;
        MinicType operand_value_type;

        operand =
            minic_c0_program_expression(context->body->program, expression->value.unary.operand);
        if (operand == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (!core_scalar_expression_value_type(context->body, operand, &operand_value_type) ||
            !minic_core_scalar_bitcast_types_valid(expression->type, operand_value_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_expression(context, expression->value.unary.operand, &operand_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (operand_value >= context->function->value_count ||
            !minic_type_equal(context->function->values[operand_value].type, operand_value_type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        return append_scalar_bitcast(
            context, expression->span, expression->type, operand_value, value_id);
    }
    if (expression->kind == MINIC_EXPRESSION_BUILTIN_OVERFLOW &&
        (expression->value.overflow.operator_kind == MINIC_OVERFLOW_ADD ||
         expression->value.overflow.operator_kind == MINIC_OVERFLOW_SUBTRACT ||
         expression->value.overflow.operator_kind == MINIC_OVERFLOW_MULTIPLY)) {
        const MinicExpression *left_expression;
        const MinicExpression *result_pointer_expression;
        const MinicExpression *right_expression;
        MinicCoreValueId left;
        MinicCoreValueId left_source;
        MinicCoreValueId result_address;
        MinicCoreValueId right;
        MinicCoreValueId right_source;
        MinicCoreLowerStatus status;
        MinicType result_type;

        if (!minic_type_equal(expression->type, minic_type_bool())) {
            return MINIC_CORE_LOWER_ERROR;
        }
        left_expression =
            minic_c0_program_expression(context->body->program, expression->value.overflow.left);
        right_expression =
            minic_c0_program_expression(context->body->program, expression->value.overflow.right);
        result_pointer_expression = minic_c0_program_expression(
            context->body->program, expression->value.overflow.result_pointer);
        if (left_expression == NULL || right_expression == NULL ||
            result_pointer_expression == NULL ||
            !minic_type_pointee(result_pointer_expression->type, &result_type) ||
            !minic_type_is_integer(result_type) || minic_type_is_bool_integer(result_type) ||
            minic_type_is_const(result_type) || minic_type_is_volatile(result_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_expression(context, expression->value.overflow.left, &left_source);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = append_integer_conversion(
            context, left_expression->span, result_type, left_source, &left);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = lower_expression(context, expression->value.overflow.right, &right_source);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = append_integer_conversion(
            context, right_expression->span, result_type, right_source, &right);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status =
            lower_expression(context, expression->value.overflow.result_pointer, &result_address);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (left >= context->function->value_count || right >= context->function->value_count ||
            result_address >= context->function->value_count ||
            !minic_type_equal(context->function->values[left].type, result_type) ||
            !minic_type_equal(context->function->values[right].type, result_type) ||
            !minic_type_equal(context->function->values[result_address].type,
                              result_pointer_expression->type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW;
        instruction.type = minic_type_bool();
        instruction.value.integer_overflow.operator_kind =
            expression->value.overflow.operator_kind == MINIC_OVERFLOW_ADD
                ? MINIC_CORE_INTEGER_OVERFLOW_ADD
            : expression->value.overflow.operator_kind == MINIC_OVERFLOW_SUBTRACT
                ? MINIC_CORE_INTEGER_OVERFLOW_SUBTRACT
                : MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY;
        instruction.value.integer_overflow.left = left;
        instruction.value.integer_overflow.right = right;
        instruction.value.integer_overflow.result_address = result_address;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_EQUAL) {
        MinicCoreValueId left;
        MinicCoreValueId right;
        MinicCoreLowerStatus status;

        if (!minic_type_equal(expression->type, minic_type_int())) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = lower_scalar_equality_operands(
            context, expression->value.binary.left, expression->value.binary.right, &left, &right);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_SCALAR_EQUAL;
        instruction.span = expression->span;
        instruction.type = minic_type_int();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = left;
        instruction.value.binary.right = right;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_NOT_EQUAL) {
        MinicCoreInstruction zero_test_instruction;
        MinicCoreValueId equal_value;
        MinicCoreValueId left;
        MinicCoreValueId right;
        MinicCoreLowerStatus status;

        if (!minic_type_equal(expression->type, minic_type_int())) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = lower_scalar_equality_operands(
            context, expression->value.binary.left, expression->value.binary.right, &left, &right);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_SCALAR_EQUAL;
        instruction.span = expression->span;
        instruction.type = minic_type_int();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = left;
        instruction.value.binary.right = right;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &equal_value)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&zero_test_instruction, 0, sizeof(zero_test_instruction));
        zero_test_instruction.kind = MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO;
        zero_test_instruction.span = expression->span;
        zero_test_instruction.type = minic_type_int();
        zero_test_instruction.result = MINIC_CORE_VALUE_INVALID;
        zero_test_instruction.value.operand = equal_value;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &zero_test_instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_LESS) {
        const MinicExpression *left_expression;
        const MinicExpression *right_expression;
        MinicCoreValueId left;
        MinicCoreValueId right;
        MinicCoreLowerStatus status;
        MinicType common_type;

        if (!minic_type_equal(expression->type, minic_type_int()) || context->target == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        left_expression =
            minic_c0_program_expression(context->body->program, expression->value.binary.left);
        right_expression =
            minic_c0_program_expression(context->body->program, expression->value.binary.right);
        if (left_expression == NULL || right_expression == NULL ||
            !minic_type_is_integer(left_expression->type) ||
            !minic_type_is_integer(right_expression->type) ||
            !minic_target_info_integer_common_for_program(context->target,
                                                          context->body->program,
                                                          left_expression->type,
                                                          right_expression->type,
                                                          &common_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_integer_binary_operands(context,
                                               expression->value.binary.left,
                                               expression->value.binary.right,
                                               common_type,
                                               &left,
                                               &right);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_LESS;
        instruction.span = expression->span;
        instruction.type = minic_type_int();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = left;
        instruction.value.binary.right = right;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_ADD &&
        minic_type_is_pointer(expression->type)) {
        const MinicExpression *left_expression;
        const MinicExpression *pointer_expression;
        const MinicExpression *right_expression;
        const MinicExpression *index_expression;
        MinicExpressionId pointer_id;
        MinicExpressionId index_id;
        MinicCoreValueId pointer_value;
        MinicCoreValueId index_value;
        MinicCoreLowerStatus status;
        size_t element_size;

        left_expression =
            minic_c0_program_expression(context->body->program, expression->value.binary.left);
        right_expression =
            minic_c0_program_expression(context->body->program, expression->value.binary.right);
        if (left_expression == NULL || right_expression == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (minic_type_is_pointer(left_expression->type) &&
            minic_type_is_integer(right_expression->type)) {
            pointer_expression = left_expression;
            index_expression = right_expression;
            pointer_id = expression->value.binary.left;
            index_id = expression->value.binary.right;
        } else if (minic_type_is_integer(left_expression->type) &&
                   minic_type_is_pointer(right_expression->type)) {
            pointer_expression = right_expression;
            index_expression = left_expression;
            pointer_id = expression->value.binary.right;
            index_id = expression->value.binary.left;
        } else {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        if (!minic_type_equal(pointer_expression->type, expression->type) ||
            !minic_c0_pointer_arithmetic_element_size(context->body->program,
                                                      minic_default_data_layout(),
                                                      expression->type,
                                                      &element_size)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_expression(context, pointer_id, &pointer_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = lower_expression(context, index_id, &index_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (pointer_value >= context->function->value_count ||
            index_value >= context->function->value_count ||
            !minic_type_equal(context->function->values[pointer_value].type,
                              pointer_expression->type) ||
            !minic_type_equal(context->function->values[index_value].type,
                              index_expression->type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        instruction.kind = MINIC_CORE_INSTRUCTION_POINTER_OFFSET;
        instruction.value.pointer_offset.base = pointer_value;
        instruction.value.pointer_offset.index = index_value;
        instruction.value.pointer_offset.element_size = element_size;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_ADD) {
        MinicCoreValueId left;
        MinicCoreValueId right;
        MinicCoreLowerStatus status;

        if (!minic_type_is_integer(expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_integer_binary_operands(context,
                                               expression->value.binary.left,
                                               expression->value.binary.right,
                                               expression->type,
                                               &left,
                                               &right);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_ADD;
        instruction.value.binary.left = left;
        instruction.value.binary.right = right;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_AND) {
        MinicCoreValueId left;
        MinicCoreValueId right;
        MinicCoreLowerStatus status;

        if (!minic_type_is_integer(expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_integer_binary_operands(context,
                                               expression->value.binary.left,
                                               expression->value.binary.right,
                                               expression->type,
                                               &left,
                                               &right);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND;
        instruction.value.binary.left = left;
        instruction.value.binary.right = right;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_OR) {
        MinicCoreValueId left;
        MinicCoreValueId right;
        MinicCoreLowerStatus status;

        if (!minic_type_is_integer(expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_integer_binary_operands(context,
                                               expression->value.binary.left,
                                               expression->value.binary.right,
                                               expression->type,
                                               &left,
                                               &right);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR;
        instruction.value.binary.left = left;
        instruction.value.binary.right = right;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_SHIFT_LEFT ||
         expression->value.binary.operator_kind == MINIC_BINARY_SHIFT_RIGHT)) {
        const MinicExpression *left_expression;
        const MinicExpression *right_expression;
        MinicCoreValueId left;
        MinicCoreValueId left_source;
        MinicCoreValueId right;
        MinicCoreLowerStatus status;

        if (!minic_type_is_integer(expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        left_expression =
            minic_c0_program_expression(context->body->program, expression->value.binary.left);
        right_expression =
            minic_c0_program_expression(context->body->program, expression->value.binary.right);
        if (left_expression == NULL || right_expression == NULL ||
            !minic_type_is_integer(left_expression->type) ||
            !minic_type_is_integer(right_expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_expression(context, expression->value.binary.left, &left_source);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = append_integer_conversion(
            context, left_expression->span, expression->type, left_source, &left);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = lower_expression(context, expression->value.binary.right, &right);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (left >= context->function->value_count || right >= context->function->value_count ||
            !minic_type_equal(context->function->values[left].type, expression->type) ||
            !minic_type_is_integer(context->function->values[right].type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        instruction.kind = expression->value.binary.operator_kind == MINIC_BINARY_SHIFT_LEFT
                               ? MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT
                               : MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT;
        instruction.value.binary.left = left;
        instruction.value.binary.right = right;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_COMPOUND_ASSIGNMENT &&
        expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_AND) {
        const MinicExpression *source;
        const MinicExpression *target;
        MinicCoreValueId address;
        MinicCoreValueId current;
        MinicCoreValueId current_common;
        MinicCoreValueId right;
        MinicCoreValueId right_common;
        MinicCoreValueId result;
        MinicCoreValueId stored_value;
        MinicCoreLowerStatus status;
        MinicType common_type;
        MinicType stored_type;

        target = minic_c0_program_expression(context->body->program, expression->value.binary.left);
        source =
            minic_c0_program_expression(context->body->program, expression->value.binary.right);
        if (target == NULL || source == NULL || target->value_category != MINIC_VALUE_LVALUE ||
            !minic_type_equal(expression->type, target->type) ||
            minic_type_is_const(target->type) ||
            !minic_type_unqualified(target->type, &stored_type) ||
            !minic_type_is_integer(stored_type) || !minic_type_is_integer(source->type) ||
            context->target == NULL ||
            !minic_target_info_integer_common_for_program(
                context->target, context->body->program, stored_type, source->type, &common_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_address(context, expression->value.binary.left, &address);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_LOAD;
        instruction.span = target->span;
        instruction.type = stored_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.load.address = address;
        instruction.value.load.is_volatile = minic_type_is_volatile(target->type);
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &current)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status =
            append_integer_conversion(context, target->span, common_type, current, &current_common);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = lower_expression(context, expression->value.binary.right, &right);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status =
            append_integer_conversion(context, source->span, common_type, right, &right_common);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND;
        instruction.span = expression->span;
        instruction.type = common_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = current_common;
        instruction.value.binary.right = right_common;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &result)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = append_integer_conversion(
            context, expression->span, stored_type, result, &stored_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
        instruction.span = expression->span;
        instruction.type = minic_type_void();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.store.address = address;
        instruction.value.store.stored_value = stored_value;
        instruction.value.store.is_volatile = minic_type_is_volatile(target->type);
        if (!minic_core_function_append_effect_instruction(
                context->function, context->block_id, &instruction)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        *value_id = stored_value;
        return MINIC_CORE_LOWER_OK;
    }
    if (minic_type_is_integer(expression->type) && context->target != NULL) {
        MinicConstValue constant;
        uint64_t constant_bits;

        if (minic_const_eval_integer(
                context->body->program, context->target, expression_id, &constant) &&
            minic_type_equal(constant.type, expression->type)) {
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
            instruction.span = expression->span;
            instruction.type = expression->type;
            instruction.result = MINIC_CORE_VALUE_INVALID;
            constant_bits = constant.bits;
            (void)memcpy(&instruction.value.integer_value,
                         &constant_bits,
                         sizeof(instruction.value.integer_value));
            return minic_core_function_append_value_instruction(
                       context->function, context->block_id, &instruction, value_id)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
    }
    return MINIC_CORE_LOWER_UNSUPPORTED;
}

static MinicCoreLowerStatus lower_assignment_pair(MinicCoreLowerContext *context,
                                                  MinicExpressionId target_id,
                                                  MinicExpressionId source_id,
                                                  MinicSourceSpan span) {
    const MinicExpression *target;
    MinicCoreInstruction instruction;
    MinicCoreValueId address_id;
    MinicCoreValueId stored_value;
    MinicCoreLowerStatus status;

    if (context == NULL || context->body == NULL || context->body->program == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    target = minic_c0_program_expression(context->body->program, target_id);
    if (target == NULL || target->value_category != MINIC_VALUE_LVALUE) {
        return MINIC_CORE_LOWER_ERROR;
    }
    {
        MinicType stored_type;

        if (!minic_type_unqualified(target->type, &stored_type) ||
            !core_memory_scalar_type(stored_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_scalar_assignment_value(context, stored_type, source_id, &stored_value);
    }
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    status = lower_address(context, target_id, &address_id);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
    instruction.span = span;
    instruction.type = minic_type_void();
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.store.address = address_id;
    instruction.value.store.stored_value = stored_value;
    instruction.value.store.is_volatile = minic_type_is_volatile(target->type);
    return minic_core_function_append_effect_instruction(
               context->function, context->block_id, &instruction)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

static MinicCoreLowerStatus lower_assignment(MinicCoreLowerContext *context,
                                             const MinicStatement *statement) {
    MinicExpressionId source_id;
    MinicExpressionId target_id;

    if (statement == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    target_id = statement->target_expression;
    source_id = statement->expression;
    return lower_assignment_pair(context, target_id, source_id, statement->span);
}

static MinicCoreLowerStatus lower_postfix_scalar_update(MinicCoreLowerContext *context,
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
    MinicType stored_type;
    bool increment;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || expression == NULL || value_id == NULL ||
        expression->kind != MINIC_EXPRESSION_UNARY ||
        (expression->value.unary.operator_kind != MINIC_UNARY_POST_INCREMENT &&
         expression->value.unary.operator_kind != MINIC_UNARY_POST_DECREMENT)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    increment = expression->value.unary.operator_kind == MINIC_UNARY_POST_INCREMENT;
    operand = minic_c0_program_expression(context->body->program, expression->value.unary.operand);
    if (operand == NULL || operand->value_category != MINIC_VALUE_LVALUE ||
        !core_memory_scalar_type(operand->type) || minic_type_is_const(operand->type) ||
        !minic_type_unqualified(operand->type, &stored_type) ||
        !minic_type_equal(expression->type, stored_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    if (minic_type_is_integer(stored_type) && minic_type_is_bool_integer(stored_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    status = lower_address(context, expression->value.unary.operand, &address);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_LOAD;
    instruction.span = expression->span;
    instruction.type = stored_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.load.address = address;
    instruction.value.load.is_volatile = minic_type_is_volatile(operand->type);
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &instruction, &current)) {
        return MINIC_CORE_LOWER_ERROR;
    }

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
    } else if (minic_type_is_pointer(stored_type)) {
        size_t element_size;

        if (!minic_c0_pointer_arithmetic_element_size(
                context->body->program, minic_default_data_layout(), stored_type, &element_size)) {
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

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
    instruction.span = expression->span;
    instruction.type = minic_type_void();
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.store.address = address;
    instruction.value.store.stored_value = updated;
    instruction.value.store.is_volatile = minic_type_is_volatile(operand->type);
    if (!minic_core_function_append_effect_instruction(
            context->function, context->block_id, &instruction)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    *value_id = current;
    return MINIC_CORE_LOWER_OK;
}

static MinicCoreLowerStatus lower_expression_statement(MinicCoreLowerContext *context,
                                                       const MinicStatement *statement) {
    const MinicExpression *expression;
    MinicExpressionId source_id;
    MinicExpressionId target_id;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        statement == NULL || statement->expression == MINIC_EXPRESSION_INVALID) {
        return MINIC_CORE_LOWER_ERROR;
    }
    expression = minic_c0_program_expression(context->body->program, statement->expression);
    if (expression == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_CALL) {
        MinicCoreValueId discarded_value;

        return lower_direct_call(context, expression, &discarded_value);
    }
    if (expression->kind == MINIC_EXPRESSION_COMPOUND_ASSIGNMENT) {
        MinicCoreValueId discarded_value;

        return lower_expression(context, statement->expression, &discarded_value);
    }
    if (expression->kind == MINIC_EXPRESSION_DISCARD) {
        MinicCoreValueId discarded_value;

        return lower_expression(context, statement->expression, &discarded_value);
    }
    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        (expression->value.unary.operator_kind == MINIC_UNARY_POST_INCREMENT ||
         expression->value.unary.operator_kind == MINIC_UNARY_POST_DECREMENT)) {
        MinicCoreValueId discarded_value;

        return lower_postfix_scalar_update(context, expression, &discarded_value);
    }
    if (expression->kind != MINIC_EXPRESSION_ASSIGNMENT) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    target_id = expression->value.binary.left;
    source_id = expression->value.binary.right;
    return lower_assignment_pair(context, target_id, source_id, expression->span);
}

static MinicCoreLowerStatus lower_return(MinicCoreLowerContext *context,
                                         const MinicStatement *statement) {
    MinicCoreTerminator terminator;
    MinicCoreLowerStatus status;

    if (context == NULL || context->source_function == NULL || statement == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&terminator, 0, sizeof(terminator));
    terminator.kind = MINIC_CORE_TERMINATOR_RETURN;
    terminator.span = statement->span;
    terminator.return_value = MINIC_CORE_VALUE_INVALID;
    if (minic_type_is_void(context->source_function->return_type)) {
        if (statement->expression != MINIC_EXPRESSION_INVALID) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
    } else {
        if (statement->expression == MINIC_EXPRESSION_INVALID) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (minic_type_is_integer(context->source_function->return_type)) {
            status = lower_integer_assignment_value(context,
                                                    context->source_function->return_type,
                                                    statement->expression,
                                                    &terminator.return_value);
        } else if (minic_type_is_pointer(context->source_function->return_type)) {
            status = lower_expression(context, statement->expression, &terminator.return_value);
            if (status == MINIC_CORE_LOWER_OK &&
                (terminator.return_value >= context->function->value_count ||
                 !minic_type_equal(context->function->values[terminator.return_value].type,
                                   context->source_function->return_type))) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
        } else {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
    }
    return minic_core_function_set_terminator(context->function, context->block_id, &terminator)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

static MinicCoreLowerStatus set_branch(MinicCoreLowerContext *context,
                                       MinicCoreBlockId block_id,
                                       MinicSourceSpan span,
                                       MinicCoreBlockId target) {
    MinicCoreTerminator terminator;

    if (context == NULL || context->function == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&terminator, 0, sizeof(terminator));
    terminator.kind = MINIC_CORE_TERMINATOR_BRANCH;
    terminator.span = span;
    terminator.return_value = MINIC_CORE_VALUE_INVALID;
    terminator.branch_target = target;
    return minic_core_function_set_terminator(context->function, block_id, &terminator)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

static MinicCoreLowerStatus lower_condition_branch(MinicCoreLowerContext *context,
                                                   MinicExpressionId expression_id,
                                                   MinicSourceSpan span,
                                                   MinicCoreBlockId when_true,
                                                   MinicCoreBlockId when_false) {
    const MinicExpression *expression;
    MinicCoreBlockId condition_block;
    MinicCoreTerminator terminator;
    MinicCoreValueId condition;
    MinicCoreLowerStatus status;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || expression_id == MINIC_EXPRESSION_INVALID ||
        when_true == MINIC_CORE_BLOCK_INVALID || when_false == MINIC_CORE_BLOCK_INVALID) {
        return MINIC_CORE_LOWER_ERROR;
    }
    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL ||
        (!minic_type_is_integer(expression->type) && !minic_type_is_pointer(expression->type))) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        expression->value.unary.operator_kind == MINIC_UNARY_LOGICAL_NOT) {
        const MinicExpression *operand;

        operand =
            minic_c0_program_expression(context->body->program, expression->value.unary.operand);
        if (operand != NULL &&
            (minic_type_is_integer(operand->type) || minic_type_is_pointer(operand->type))) {
            return lower_condition_branch(
                context, expression->value.unary.operand, span, when_false, when_true);
        }
    }
    if (expression->kind == MINIC_EXPRESSION_CONVERSION && context->target != NULL) {
        const MinicExpression *operand;
        unsigned int source_width;
        unsigned int destination_width;

        operand =
            minic_c0_program_expression(context->body->program, expression->value.unary.operand);
        if (operand != NULL && minic_type_is_integer(operand->type) &&
            minic_type_is_integer(expression->type) &&
            minic_target_info_integer_width(
                context->target, context->body->program, operand->type, &source_width) &&
            minic_target_info_integer_width(
                context->target, context->body->program, expression->type, &destination_width) &&
            (minic_type_equal(operand->type, expression->type) ||
             destination_width > source_width)) {
            return lower_condition_branch(
                context, expression->value.unary.operand, span, when_true, when_false);
        }
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_AND) {
        MinicCoreBlockId right_block;

        if (!minic_core_function_add_block(context->function, &right_block)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = lower_condition_branch(
            context, expression->value.binary.left, span, right_block, when_false);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        context->block_id = right_block;
        return lower_condition_branch(
            context, expression->value.binary.right, span, when_true, when_false);
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_OR) {
        MinicCoreBlockId right_block;

        if (!minic_core_function_add_block(context->function, &right_block)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = lower_condition_branch(
            context, expression->value.binary.left, span, when_true, right_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        context->block_id = right_block;
        return lower_condition_branch(
            context, expression->value.binary.right, span, when_true, when_false);
    }

    status = lower_expression(context, expression_id, &condition);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    if (condition >= context->function->value_count) {
        return MINIC_CORE_LOWER_ERROR;
    }
    if (minic_type_is_pointer(expression->type)) {
        MinicCoreInstruction zero_test;
        MinicCoreBlockId original_true;

        if (!minic_type_is_pointer(context->function->values[condition].type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&zero_test, 0, sizeof(zero_test));
        zero_test.kind = MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO;
        zero_test.span = span;
        zero_test.type = minic_type_int();
        zero_test.result = MINIC_CORE_VALUE_INVALID;
        zero_test.value.operand = condition;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &zero_test, &condition)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        original_true = when_true;
        when_true = when_false;
        when_false = original_true;
    } else if (!minic_type_is_integer(context->function->values[condition].type)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    condition_block = context->block_id;
    (void)memset(&terminator, 0, sizeof(terminator));
    terminator.kind = MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH;
    terminator.span = span;
    terminator.return_value = MINIC_CORE_VALUE_INVALID;
    terminator.conditional.condition = condition;
    terminator.conditional.when_true = when_true;
    terminator.conditional.when_false = when_false;
    return minic_core_function_set_terminator(context->function, condition_block, &terminator)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

static MinicCoreLowerStatus
lower_if(MinicCoreLowerContext *context, const MinicStatement *statement, bool *terminated) {
    const MinicBlock *else_source;
    const MinicBlock *then_source;
    const MinicExpression *condition_expression;
    MinicCoreBlockId condition_block;
    MinicCoreBlockId else_block;
    MinicCoreBlockId false_target;
    MinicCoreBlockId merge_block;
    MinicCoreBlockId then_block;
    MinicCoreBlockId then_continuation_block;
    MinicCoreBlockId else_continuation_block;
    MinicCoreBlockId continuation_block;
    MinicCoreLowerStatus status;
    bool else_terminated;
    bool needs_merge;
    bool then_terminated;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || statement == NULL || terminated == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    condition_expression =
        minic_c0_program_expression(context->body->program, statement->expression);
    then_source = minic_c0_program_block(context->body->program, statement->then_block);
    if (condition_expression == NULL || then_source == NULL ||
        (!minic_type_is_integer(condition_expression->type) &&
         !minic_type_is_pointer(condition_expression->type))) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    else_source = NULL;
    if (statement->else_block != MINIC_BLOCK_INVALID) {
        else_source = minic_c0_program_block(context->body->program, statement->else_block);
        if (else_source == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
    }

    condition_block = context->block_id;
    if (!minic_core_function_add_block(context->function, &then_block)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    else_block = MINIC_CORE_BLOCK_INVALID;
    if (else_source != NULL && !minic_core_function_add_block(context->function, &else_block)) {
        return MINIC_CORE_LOWER_ERROR;
    }

    context->block_id = then_block;
    status = lower_block(context, then_source, &then_terminated);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    then_continuation_block = context->block_id;
    else_continuation_block = MINIC_CORE_BLOCK_INVALID;
    else_terminated = false;
    if (else_source != NULL) {
        context->block_id = else_block;
        status = lower_block(context, else_source, &else_terminated);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        else_continuation_block = context->block_id;
    }

    needs_merge = !then_terminated || else_source == NULL || !else_terminated;
    merge_block = MINIC_CORE_BLOCK_INVALID;
    if (needs_merge) {
        if (!minic_core_function_add_block(context->function, &merge_block)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (!then_terminated) {
            status = set_branch(context, then_continuation_block, statement->span, merge_block);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
        }
        if (else_source != NULL && !else_terminated) {
            status = set_branch(context, else_continuation_block, statement->span, merge_block);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
        }
        false_target = else_source == NULL ? merge_block : else_block;
        context->block_id = merge_block;
    } else {
        false_target = else_block;
        context->block_id = condition_block;
    }

    continuation_block = context->block_id;
    context->block_id = condition_block;
    status = lower_condition_branch(
        context, statement->expression, statement->span, then_block, false_target);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    context->block_id = continuation_block;
    *terminated = !needs_merge;
    return MINIC_CORE_LOWER_OK;
}

static bool internal_while_label_pair(const MinicStatement *label, const MinicStatement *loop) {
    bool same_begin;

    if (label == NULL || loop == NULL) {
        return false;
    }
    same_begin = label->span.begin.offset == loop->span.begin.offset &&
                 label->span.begin.line == loop->span.begin.line &&
                 label->span.begin.column == loop->span.begin.column;
    return label->kind == MINIC_STATEMENT_LABEL && loop->kind == MINIC_STATEMENT_WHILE &&
           same_begin && label->target_expression == MINIC_EXPRESSION_INVALID &&
           label->expression == MINIC_EXPRESSION_INVALID &&
           label->target_statement == MINIC_STATEMENT_INVALID;
}

static bool source_position_equal(MinicSourcePosition left, MinicSourcePosition right) {
    return left.offset == right.offset && left.line == right.line && left.column == right.column;
}

static bool normalized_do_while_zero_body(const MinicCoreLowerContext *context,
                                          const MinicStatement *loop,
                                          const MinicBlock *body,
                                          MinicBlock *single_iteration_body) {
    const MinicExpression *loop_condition;
    const MinicExpression *negated_condition;
    const MinicExpression *source_condition;
    const MinicStatement *continue_label;
    const MinicStatement *condition_check;
    const MinicStatement *break_statement;
    const MinicBlock *break_block;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        loop == NULL || body == NULL || single_iteration_body == NULL ||
        body->statement_count < 2U) {
        return false;
    }
    loop_condition = minic_c0_program_expression(context->body->program, loop->expression);
    continue_label = minic_c0_program_statement(context->body->program,
                                                body->statements[body->statement_count - 2U]);
    condition_check = minic_c0_program_statement(context->body->program,
                                                 body->statements[body->statement_count - 1U]);
    if (loop_condition == NULL || loop_condition->kind != MINIC_EXPRESSION_INTEGER ||
        !minic_type_is_integer(loop_condition->type) || loop_condition->value.integer_value != 1 ||
        continue_label == NULL || continue_label->kind != MINIC_STATEMENT_LABEL ||
        continue_label->target_expression != MINIC_EXPRESSION_INVALID ||
        continue_label->expression != MINIC_EXPRESSION_INVALID ||
        continue_label->target_statement != MINIC_STATEMENT_INVALID ||
        !source_position_equal(continue_label->span.begin, loop->span.begin) ||
        condition_check == NULL || condition_check->kind != MINIC_STATEMENT_IF ||
        condition_check->expression == MINIC_EXPRESSION_INVALID ||
        condition_check->then_block == MINIC_BLOCK_INVALID ||
        condition_check->else_block != MINIC_BLOCK_INVALID ||
        !source_position_equal(condition_check->span.begin, loop->span.begin)) {
        return false;
    }
    negated_condition =
        minic_c0_program_expression(context->body->program, condition_check->expression);
    if (negated_condition == NULL || negated_condition->kind != MINIC_EXPRESSION_UNARY ||
        negated_condition->value.unary.operator_kind != MINIC_UNARY_LOGICAL_NOT) {
        return false;
    }
    source_condition =
        minic_c0_program_expression(context->body->program, negated_condition->value.unary.operand);
    if (source_condition == NULL || source_condition->kind != MINIC_EXPRESSION_INTEGER ||
        !minic_type_is_integer(source_condition->type) ||
        source_condition->value.integer_value != 0) {
        return false;
    }
    break_block = minic_c0_program_block(context->body->program, condition_check->then_block);
    if (break_block == NULL || break_block->statement_count != 1U) {
        return false;
    }
    break_statement =
        minic_c0_program_statement(context->body->program, break_block->statements[0]);
    if (break_statement == NULL || break_statement->kind != MINIC_STATEMENT_BREAK ||
        break_statement->cleanup_context != MINIC_CLEANUP_CONTEXT_ROOT ||
        break_statement->cleanup_stop_context != MINIC_CLEANUP_CONTEXT_ROOT ||
        !source_position_equal(break_statement->span.begin, loop->span.begin)) {
        return false;
    }

    *single_iteration_body = *body;
    single_iteration_body->statement_count -= 2U;
    return true;
}

static bool normalized_for_update_tail(const MinicCoreLowerContext *context,
                                       const MinicStatement *loop,
                                       const MinicBlock *body,
                                       MinicBlock *iteration_body,
                                       const MinicStatement **update_statement) {
    const MinicStatement *continue_label;
    const MinicStatement *update;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        loop == NULL || body == NULL || iteration_body == NULL || update_statement == NULL ||
        body->statement_count < 2U) {
        return false;
    }
    continue_label = minic_c0_program_statement(context->body->program,
                                                body->statements[body->statement_count - 2U]);
    update = minic_c0_program_statement(context->body->program,
                                        body->statements[body->statement_count - 1U]);
    if (continue_label == NULL || continue_label->kind != MINIC_STATEMENT_LABEL ||
        continue_label->target_expression != MINIC_EXPRESSION_INVALID ||
        continue_label->expression != MINIC_EXPRESSION_INVALID ||
        continue_label->target_statement != MINIC_STATEMENT_INVALID ||
        !source_position_equal(continue_label->span.begin, loop->span.begin) || update == NULL ||
        update->kind != MINIC_STATEMENT_EXPRESSION ||
        update->cleanup_context != MINIC_CLEANUP_CONTEXT_ROOT ||
        update->cleanup_stop_context != MINIC_CLEANUP_CONTEXT_ROOT ||
        update->expression == MINIC_EXPRESSION_INVALID) {
        return false;
    }
    *iteration_body = *body;
    iteration_body->statement_count -= 2U;
    *update_statement = update;
    return true;
}

static MinicCoreLowerStatus
lower_while(MinicCoreLowerContext *context, const MinicStatement *statement, bool *terminated) {
    const MinicBlock *body_source;
    const MinicBlock *iteration_source;
    const MinicExpression *condition_expression;
    const MinicStatement *for_update;
    MinicBlock normalized_for_body;
    MinicCoreBlockId body_block;
    MinicCoreBlockId condition_block;
    MinicCoreBlockId exit_block;
    MinicCoreBlockId preheader_block;
    MinicCoreTerminator terminator;
    MinicCoreValueId condition;
    MinicCoreLowerStatus status;
    bool body_terminated;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || statement == NULL || terminated == NULL ||
        statement->kind != MINIC_STATEMENT_WHILE ||
        statement->cleanup_context != MINIC_CLEANUP_CONTEXT_ROOT ||
        statement->cleanup_stop_context != MINIC_CLEANUP_CONTEXT_ROOT ||
        statement->expression == MINIC_EXPRESSION_INVALID ||
        statement->then_block == MINIC_BLOCK_INVALID ||
        statement->else_block != MINIC_BLOCK_INVALID) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    condition_expression =
        minic_c0_program_expression(context->body->program, statement->expression);
    body_source = minic_c0_program_block(context->body->program, statement->then_block);
    if (condition_expression == NULL || body_source == NULL ||
        !minic_type_is_integer(condition_expression->type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    {
        MinicBlock single_iteration_body;

        if (normalized_do_while_zero_body(
                context, statement, body_source, &single_iteration_body)) {
            status = lower_block(context, &single_iteration_body, &body_terminated);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            *terminated = body_terminated;
            return MINIC_CORE_LOWER_OK;
        }
    }

    iteration_source = body_source;
    for_update = NULL;
    if (normalized_for_update_tail(
            context, statement, body_source, &normalized_for_body, &for_update)) {
        iteration_source = &normalized_for_body;
    }

    preheader_block = context->block_id;
    if (!minic_core_function_add_block(context->function, &condition_block) ||
        !minic_core_function_add_block(context->function, &body_block) ||
        !minic_core_function_add_block(context->function, &exit_block)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    status = set_branch(context, preheader_block, statement->span, condition_block);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }

    context->block_id = condition_block;
    status = lower_expression(context, statement->expression, &condition);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    if (!minic_type_is_integer(context->function->values[condition].type)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&terminator, 0, sizeof(terminator));
    terminator.kind = MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH;
    terminator.span = statement->span;
    terminator.return_value = MINIC_CORE_VALUE_INVALID;
    terminator.conditional.condition = condition;
    terminator.conditional.when_true = body_block;
    terminator.conditional.when_false = exit_block;
    if (!minic_core_function_set_terminator(context->function, condition_block, &terminator)) {
        return MINIC_CORE_LOWER_ERROR;
    }

    context->block_id = body_block;
    status = lower_block(context, iteration_source, &body_terminated);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    if (!body_terminated && for_update != NULL) {
        status = lower_expression_statement(context, for_update);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
    }
    if (!body_terminated) {
        status = set_branch(context, context->block_id, statement->span, condition_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
    }
    context->block_id = exit_block;
    *terminated = false;
    return MINIC_CORE_LOWER_OK;
}

static MinicCoreLowerStatus lower_opaque_inline_asm(MinicCoreLowerContext *context,
                                                    const MinicStatement *statement) {
    const MinicInlineAsm *source;
    MinicCoreInlineAsmId inline_asm_id;
    MinicCoreInstruction instruction;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || statement == NULL ||
        statement->inline_asm_id == MINIC_INLINE_ASM_INVALID) {
        return MINIC_CORE_LOWER_ERROR;
    }
    source = minic_c0_program_inline_asm(context->body->program, statement->inline_asm_id);
    if (source == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    if (!source->is_volatile || source->is_goto || source->template_text == NULL ||
        source->template_length == 0U || source->output_count != 0U || source->input_count != 0U ||
        source->label_count != 0U || source->register_clobber_count != 0U) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    if (!minic_core_function_add_opaque_inline_asm(context->function,
                                                   source->template_text,
                                                   source->template_length,
                                                   source->is_volatile,
                                                   source->has_memory_clobber,
                                                   &inline_asm_id)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM;
    instruction.span = statement->span;
    instruction.type = minic_type_void();
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.inline_asm_id = inline_asm_id;
    return minic_core_function_append_effect_instruction(
               context->function, context->block_id, &instruction)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

static MinicCoreLowerStatus
lower_block(MinicCoreLowerContext *context, const MinicBlock *source_block, bool *terminated) {
    size_t statement_index;
    bool block_terminated;

    if (context == NULL || source_block == NULL || terminated == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    block_terminated = false;
    for (statement_index = 0U; statement_index < source_block->statement_count; ++statement_index) {
        const MinicStatement *statement;
        MinicCoreLowerStatus status;
        bool statement_terminated;

        statement = minic_c0_program_statement(context->body->program,
                                               source_block->statements[statement_index]);
        if (statement == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (block_terminated) {
            if (statement->kind != MINIC_STATEMENT_RETURN) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            continue;
        }
        if (statement->cleanup_context != MINIC_CLEANUP_CONTEXT_ROOT ||
            statement->cleanup_stop_context != MINIC_CLEANUP_CONTEXT_ROOT) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        statement_terminated = false;
        if (statement->kind == MINIC_STATEMENT_LABEL) {
            const MinicStatement *loop;
            MinicStatementId next_statement_id;

            if (statement_index + 1U >= source_block->statement_count) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            next_statement_id = source_block->statements[statement_index + 1U];
            loop = minic_c0_program_statement(context->body->program, next_statement_id);
            if (!internal_while_label_pair(statement, loop)) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            status = lower_while(context, loop, &statement_terminated);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            statement_index += 1U;
        } else {
            switch (statement->kind) {
            case MINIC_STATEMENT_ASSIGN:
                status = lower_assignment(context, statement);
                break;
            case MINIC_STATEMENT_EXPRESSION:
                status = lower_expression_statement(context, statement);
                break;
            case MINIC_STATEMENT_INLINE_ASM:
                status = lower_opaque_inline_asm(context, statement);
                break;
            case MINIC_STATEMENT_RETURN:
                status = lower_return(context, statement);
                statement_terminated = status == MINIC_CORE_LOWER_OK;
                break;
            case MINIC_STATEMENT_IF:
                status = lower_if(context, statement, &statement_terminated);
                break;
            case MINIC_STATEMENT_WHILE:
                status = lower_while(context, statement, &statement_terminated);
                break;
            default:
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
        }
        block_terminated = statement_terminated;
    }
    *terminated = block_terminated;
    return MINIC_CORE_LOWER_OK;
}

MinicCoreLowerStatus minic_core_lower_function(const MinicFunctionBodyView *body,
                                               const MinicTargetInfo *target,
                                               MinicCoreFunction *output) {
    const MinicFunction *source_function;
    const MinicBlock *source_block;
    MinicCoreFunction lowered;
    MinicCoreLowerContext context;
    MinicCoreBlockId block_id;
    MinicCoreObjectId *local_objects;
    MinicCoreLowerStatus status;
    size_t local_index;
    bool terminated;

    if (body == NULL || body->program == NULL || target == NULL || output == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    source_function = minic_c0_function_body_function(body);
    source_block = minic_c0_program_block(body->program, minic_c0_function_body_root_block(body));
    if (source_function == NULL || source_block == NULL || source_function->name == NULL ||
        source_function->name_length == 0U) {
        return MINIC_CORE_LOWER_ERROR;
    }
    if (source_block->statement_count == 0U && !minic_type_is_void(source_function->return_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    if (source_function->local_count > SIZE_MAX / sizeof(*local_objects)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    local_objects =
        source_function->local_count == 0U
            ? NULL
            : (MinicCoreObjectId *)malloc(source_function->local_count * sizeof(*local_objects));
    if (source_function->local_count != 0U && local_objects == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    for (local_index = 0U; local_index < source_function->local_count; ++local_index) {
        local_objects[local_index] = MINIC_CORE_OBJECT_INVALID;
    }

    minic_core_function_initialize(&lowered);
    if (!minic_core_function_set_signature(&lowered,
                                           source_function->name,
                                           source_function->name_length,
                                           source_function->return_type,
                                           source_function->parameter_types,
                                           source_function->parameter_count) ||
        !minic_core_function_add_block(&lowered, &block_id)) {
        free(local_objects);
        minic_core_function_destroy(&lowered);
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&context, 0, sizeof(context));
    context.body = body;
    context.source_function = source_function;
    context.target = target;
    context.function = &lowered;
    context.block_id = block_id;
    context.local_objects = local_objects;
    status = lower_parameter_ingress(&context);
    terminated = false;
    if (status == MINIC_CORE_LOWER_OK) {
        status = lower_block(&context, source_block, &terminated);
    }
    free(local_objects);
    if (status != MINIC_CORE_LOWER_OK) {
        minic_core_function_destroy(&lowered);
        return status;
    }
    if (!terminated && minic_type_is_void(source_function->return_type)) {
        MinicCoreTerminator terminator;

        (void)memset(&terminator, 0, sizeof(terminator));
        terminator.kind = MINIC_CORE_TERMINATOR_RETURN;
        terminator.return_value = MINIC_CORE_VALUE_INVALID;
        if (!minic_core_function_set_terminator(&lowered, context.block_id, &terminator)) {
            minic_core_function_destroy(&lowered);
            return MINIC_CORE_LOWER_ERROR;
        }
        terminated = true;
    }
    if (!terminated) {
        minic_core_function_destroy(&lowered);
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    if (!minic_core_function_verify(&lowered)) {
        minic_core_function_destroy(&lowered);
        return MINIC_CORE_LOWER_ERROR;
    }
    minic_core_function_destroy(output);
    *output = lowered;
    return MINIC_CORE_LOWER_OK;
}
