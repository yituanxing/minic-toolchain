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
static MinicCoreLowerStatus lower_scalar_update(MinicCoreLowerContext *context,
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
static MinicCoreLowerStatus append_scalar_bitcast(MinicCoreLowerContext *context,
                                                  MinicSourceSpan span,
                                                  MinicType target_type,
                                                  MinicCoreValueId source_value,
                                                  MinicCoreValueId *value_id);

static bool core_memory_scalar_type(MinicType type) {
    return minic_type_is_integer(type) || minic_type_is_pointer(type);
}

static bool core_global_addressable_type(MinicType type) {
    return core_memory_scalar_type(type) || minic_type_is_array(type);
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
    if (local->is_array ||
        minic_c0_program_local_fixed_register_binding(context->body->program, local_id) != NULL ||
        (!core_memory_scalar_type(local->type) && !minic_type_is_record(local->type))) {
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
        MinicCoreLowerStatus status;
        MinicLocalId local_id;
        MinicType parameter_value_type;

        local_id = context->source_function->local_begin + parameter_index;
        parameter = minic_c0_program_local(context->body->program, local_id);
        if (parameter == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (minic_type_is_volatile(parameter->type) || parameter->is_array ||
            parameter->is_register_storage ||
            !minic_type_unqualified(parameter->type, &parameter_value_type) ||
            !minic_type_equal(parameter_value_type,
                              context->source_function->parameter_types[parameter_index])) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_local_object(context, local_id, &object_id);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }

        if (minic_type_is_record(parameter_value_type)) {
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT;
            instruction.span = parameter->name_span;
            instruction.type = minic_type_void();
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.parameter_object.parameter_index = parameter_index;
            instruction.value.parameter_object.object_id = object_id;
            if (!minic_core_function_append_effect_instruction(
                    context->function, context->block_id, &instruction)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            continue;
        }
        if (!core_memory_scalar_type(parameter_value_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        {
            MinicCoreValueId address_id;
            MinicCoreValueId parameter_value;
            MinicType pointer_type;

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
        if (!core_global_addressable_type(global->type)) {
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
        MinicArrayObjectInfo array_info;
        MinicCoreInstruction offset_instruction;
        MinicCoreObjectId base_object;
        MinicCoreValueId base_value;
        MinicCoreValueId index_value;
        MinicCoreLowerStatus subscript_status;
        MinicType array_pointer_type;
        MinicType element_type;
        MinicType pointer_type;
        size_t element_size;
        bool array_base;

        base =
            minic_c0_program_expression(context->body->program, expression->value.subscript.base);
        index =
            minic_c0_program_expression(context->body->program, expression->value.subscript.index);
        if (base == NULL || index == NULL || !minic_type_is_integer(index->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        (void)memset(&array_info, 0, sizeof(array_info));
        array_base = minic_c0_expression_array_object_info(
            context->body->program, base, &array_info);
        if (array_base) {
            if (!array_info.has_materialized_type || !minic_type_is_array(base->type) ||
                !minic_type_equal(array_info.element_type, expression->type) ||
                !minic_type_pointer_to(array_info.element_type, &pointer_type) ||
                !minic_c0_pointer_arithmetic_element_size(context->body->program,
                                                          minic_default_data_layout(),
                                                          pointer_type,
                                                          &element_size)) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            subscript_status =
                lower_address(context, expression->value.subscript.base, &base_value);
            if (subscript_status != MINIC_CORE_LOWER_OK) {
                return subscript_status;
            }
            if (base_value >= context->function->value_count ||
                !minic_type_pointer_to(base->type, &array_pointer_type) ||
                !minic_type_equal(context->function->values[base_value].type,
                                  array_pointer_type)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            subscript_status = append_scalar_bitcast(
                context, base->span, pointer_type, base_value, &base_value);
            if (subscript_status != MINIC_CORE_LOWER_OK) {
                return subscript_status;
            }
        } else {
            if (!minic_type_is_pointer(base->type) ||
                !minic_type_pointee(base->type, &element_type) ||
                !minic_type_equal(element_type, expression->type) ||
                !minic_c0_pointer_arithmetic_element_size(context->body->program,
                                                          minic_default_data_layout(),
                                                          base->type,
                                                          &element_size)) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            pointer_type = base->type;
            subscript_status =
                lower_expression(context, expression->value.subscript.base, &base_value);
            if (subscript_status != MINIC_CORE_LOWER_OK) {
                return subscript_status;
            }
            if (base_value >= context->function->value_count ||
                !minic_type_equal(context->function->values[base_value].type, base->type)) {
                return MINIC_CORE_LOWER_ERROR;
            }
        }
        subscript_status =
            spill_scalar_value(context, base->span, pointer_type, base_value, &base_object);
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
            reload_scalar_value(context, base->span, pointer_type, base_object, &base_value);
        if (subscript_status != MINIC_CORE_LOWER_OK) {
            return subscript_status;
        }

        (void)memset(&offset_instruction, 0, sizeof(offset_instruction));
        offset_instruction.kind = MINIC_CORE_INSTRUCTION_POINTER_OFFSET;
        offset_instruction.span = expression->span;
        offset_instruction.type = pointer_type;
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
    if (integer_comparison) {
        status = append_integer_conversion(
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
    if (integer_comparison) {
        status = append_integer_conversion(
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
    MinicCoreObjectId argument_objects[MINIC_MAX_FUNCTION_PARAMETERS];
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
        status = spill_scalar_value(context,
                                    expression->span,
                                    callee->parameter_types[argument_index],
                                    arguments[argument_index],
                                    &argument_objects[argument_index]);
        if (status != MINIC_CORE_LOWER_OK) {
            free(arguments);
            return status;
        }
    }
    for (argument_index = 0U; argument_index < callee->parameter_count; ++argument_index) {
        status = reload_scalar_value(context,
                                     expression->span,
                                     callee->parameter_types[argument_index],
                                     argument_objects[argument_index],
                                     &arguments[argument_index]);
        if (status != MINIC_CORE_LOWER_OK) {
            free(arguments);
            return status;
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

        /* M50B_EFFECT_ONLY_STATEMENT_EXPRESSION: a GNU ({ ... }) whose last
           statement has no value is an effect expression, not a scalar one. */
        statement_block = minic_c0_program_block(context->body->program,
                                                 expression->value.statement_expression.block);
        if (statement_block == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (expression->value.statement_expression.result == MINIC_EXPRESSION_INVALID) {
            if (!minic_type_is_void(expression->type)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            status = lower_block(context, statement_block, &terminated);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            if (terminated) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            *value_id = MINIC_CORE_VALUE_INVALID;
            return MINIC_CORE_LOWER_OK;
        }
        if (!core_scalar_expression_value_type(context->body, expression, &result_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        statement_result = minic_c0_program_expression(
            context->body->program, expression->value.statement_expression.result);
        if (statement_result == NULL) {
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
    /* M57_CONSTANT_CONDITIONAL_PRUNING: if the frontend can prove
       the condition, lower only the selected arm. Besides being smaller CFG,
       this is semantically important for GNU compile-time choice idioms: the
       dead arm may contain target builtins that are never evaluated. */
    if (expression->kind == MINIC_EXPRESSION_CONDITIONAL &&
        !expression->value.conditional.uses_condition_value &&
        expression->value.conditional.when_true != MINIC_EXPRESSION_INVALID &&
        expression->value.conditional.when_false != MINIC_EXPRESSION_INVALID &&
        context->target != NULL) {
        MinicConstValue condition_value;
        MinicExpressionId selected_expression;
        bool condition_is_zero;

        if (minic_const_eval_integer(context->body->program,
                                     context->target,
                                     expression->value.conditional.condition,
                                     &condition_value) &&
            minic_const_value_is_zero(context->body->program,
                                      context->target,
                                      &condition_value,
                                      &condition_is_zero)) {
            selected_expression = condition_is_zero
                                      ? expression->value.conditional.when_false
                                      : expression->value.conditional.when_true;
            if (minic_type_is_void(expression->type)) {
                MinicCoreLowerStatus status;
                MinicCoreValueId discarded_value;

                status = lower_expression(context, selected_expression, &discarded_value);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
                if (discarded_value != MINIC_CORE_VALUE_INVALID) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                *value_id = MINIC_CORE_VALUE_INVALID;
                return MINIC_CORE_LOWER_OK;
            }
            if (!core_memory_scalar_type(expression->type)) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            return lower_scalar_assignment_value(
                context, expression->type, selected_expression, value_id);
        }
    }
    /* M53_VOID_CONDITIONAL_EXPRESSION: C permits an effect-only
       conditional when both arms have void type. Model it as CFG only; there is
       deliberately no synthetic scalar result or spill object. */
    if (expression->kind == MINIC_EXPRESSION_CONDITIONAL &&
        !expression->value.conditional.uses_condition_value &&
        expression->value.conditional.when_true != MINIC_EXPRESSION_INVALID &&
        expression->value.conditional.when_false != MINIC_EXPRESSION_INVALID &&
        minic_type_is_void(expression->type)) {
        const MinicExpression *false_expression;
        const MinicExpression *true_expression;
        MinicCoreBlockId false_block;
        MinicCoreBlockId merge_block;
        MinicCoreBlockId true_block;
        MinicCoreValueId discarded_value;
        MinicCoreLowerStatus status;

        true_expression = minic_c0_program_expression(
            context->body->program, expression->value.conditional.when_true);
        false_expression = minic_c0_program_expression(
            context->body->program, expression->value.conditional.when_false);
        if (true_expression == NULL || false_expression == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (!minic_type_is_void(true_expression->type) ||
            !minic_type_is_void(false_expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        if (!minic_core_function_add_block(context->function, &true_block) ||
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
        status = lower_expression(
            context, expression->value.conditional.when_true, &discarded_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (discarded_value != MINIC_CORE_VALUE_INVALID) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = set_branch(context, context->block_id, expression->span, merge_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }

        context->block_id = false_block;
        status = lower_expression(
            context, expression->value.conditional.when_false, &discarded_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (discarded_value != MINIC_CORE_VALUE_INVALID) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = set_branch(context, context->block_id, expression->span, merge_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }

        context->block_id = merge_block;
        *value_id = MINIC_CORE_VALUE_INVALID;
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
        /* M55_SCALAR_CONDITIONAL_ARM_CONVERSION: the frontend owns the
           conditional result type. The selected arm undergoes the same scalar
           conversion as assignment to that type; its source type need not
           already be identical. */
        if (!core_memory_scalar_type(expression->type) ||
            !core_scalar_expression_value_type(context->body, true_expression, &true_type) ||
            !core_scalar_expression_value_type(context->body, false_expression, &false_type)) {
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
        status = lower_scalar_assignment_value(context,
                                               expression->type,
                                               expression->value.conditional.when_true,
                                               &arm_value);
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
        status = lower_scalar_assignment_value(context,
                                               expression->type,
                                               expression->value.conditional.when_false,
                                               &arm_value);
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
    /* M58_LOGICAL_OR_VALUE: lower_condition_branch already owns the
       short-circuit semantics for both && and ||. Their value materialization
       is identical: branch to true/false, store 1/0, then reload. */
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_AND ||
         expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_OR)) {
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
    if (expression->kind == MINIC_EXPRESSION_FIXED_REGISTER) {
        const MinicFixedRegisterBinding *binding;

        binding = minic_c0_program_fixed_register_binding(
            context->body->program, expression->value.fixed_register_binding_id);
        if (binding == NULL || binding->register_name == NULL ||
            binding->register_name_length == 0U) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (!core_memory_scalar_type(binding->type) ||
            !minic_type_equal(binding->type, expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_FIXED_REGISTER_READ;
        instruction.span = expression->span;
        instruction.type = expression->type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.fixed_register_binding_id =
            expression->value.fixed_register_binding_id;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        (expression->value.unary.operator_kind == MINIC_UNARY_POST_INCREMENT ||
         expression->value.unary.operator_kind == MINIC_UNARY_POST_DECREMENT ||
         expression->value.unary.operator_kind == MINIC_UNARY_PRE_INCREMENT ||
         expression->value.unary.operator_kind == MINIC_UNARY_PRE_DECREMENT)) {
        return lower_scalar_update(context, expression, value_id);
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
        MinicCoreObjectId left_object;
        MinicCoreObjectId right_object;
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
        status =
            spill_scalar_value(context, left_expression->span, result_type, left, &left_object);
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
            spill_scalar_value(context, right_expression->span, result_type, right, &right_object);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status =
            lower_expression(context, expression->value.overflow.result_pointer, &result_address);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status =
            reload_scalar_value(context, left_expression->span, result_type, left_object, &left);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status =
            reload_scalar_value(context, right_expression->span, result_type, right_object, &right);
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
        (expression->value.binary.operator_kind == MINIC_BINARY_LESS_EQUAL ||
         expression->value.binary.operator_kind == MINIC_BINARY_GREATER ||
         expression->value.binary.operator_kind == MINIC_BINARY_GREATER_EQUAL)) {
        const MinicExpression *left_expression;
        const MinicExpression *right_expression;
        MinicCoreInstruction invert_instruction;
        MinicCoreValueId left;
        MinicCoreValueId less_value;
        MinicCoreValueId right;
        MinicCoreLowerStatus status;
        MinicType common_type;
        MinicType left_type;
        MinicType right_type;
        bool invert;
        bool swap;

        if (!minic_type_equal(expression->type, minic_type_int()) || context->target == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        left_expression =
            minic_c0_program_expression(context->body->program, expression->value.binary.left);
        right_expression =
            minic_c0_program_expression(context->body->program, expression->value.binary.right);
        if (left_expression == NULL || right_expression == NULL ||
            !core_scalar_expression_value_type(context->body, left_expression, &left_type) ||
            !core_scalar_expression_value_type(context->body, right_expression, &right_type) ||
            !minic_type_is_integer(left_type) || !minic_type_is_integer(right_type) ||
            !minic_target_info_integer_common_for_program(
                context->target, context->body->program, left_type, right_type, &common_type)) {
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
        swap = expression->value.binary.operator_kind == MINIC_BINARY_GREATER ||
               expression->value.binary.operator_kind == MINIC_BINARY_LESS_EQUAL;
        invert = expression->value.binary.operator_kind == MINIC_BINARY_LESS_EQUAL ||
                 expression->value.binary.operator_kind == MINIC_BINARY_GREATER_EQUAL;
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_LESS;
        instruction.span = expression->span;
        instruction.type = minic_type_int();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = swap ? right : left;
        instruction.value.binary.right = swap ? left : right;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &less_value)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (!invert) {
            *value_id = less_value;
            return MINIC_CORE_LOWER_OK;
        }
        (void)memset(&invert_instruction, 0, sizeof(invert_instruction));
        invert_instruction.kind = MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO;
        invert_instruction.span = expression->span;
        invert_instruction.type = minic_type_int();
        invert_instruction.result = MINIC_CORE_VALUE_INVALID;
        invert_instruction.value.operand = less_value;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &invert_instruction, value_id)
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
        MinicCoreObjectId pointer_object;
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
        status = spill_scalar_value(context,
                                    pointer_expression->span,
                                    pointer_expression->type,
                                    pointer_value,
                                    &pointer_object);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = lower_expression(context, index_id, &index_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = reload_scalar_value(context,
                                     pointer_expression->span,
                                     pointer_expression->type,
                                     pointer_object,
                                     &pointer_value);
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
        (expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT ||
         expression->value.binary.operator_kind == MINIC_BINARY_MULTIPLY ||
         expression->value.binary.operator_kind == MINIC_BINARY_DIVIDE ||
         expression->value.binary.operator_kind == MINIC_BINARY_REMAINDER ||
         expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_XOR)) {
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
        switch (expression->value.binary.operator_kind) {
        case MINIC_BINARY_SUBTRACT:
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT;
            break;
        case MINIC_BINARY_MULTIPLY:
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY;
            break;
        case MINIC_BINARY_DIVIDE:
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE;
            break;
        case MINIC_BINARY_REMAINDER:
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER;
            break;
        case MINIC_BINARY_BITWISE_XOR:
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR;
            break;
        default:
            return MINIC_CORE_LOWER_ERROR;
        }
        instruction.value.binary.left = left;
        instruction.value.binary.right = right;
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
        MinicCoreObjectId left_object;
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
        status = spill_scalar_value(
            context, left_expression->span, expression->type, left, &left_object);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = lower_expression(context, expression->value.binary.right, &right);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = reload_scalar_value(
            context, left_expression->span, expression->type, left_object, &left);
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
    /* M51_SHIFT_COMPOUND_ASSIGNMENT: shifts use integer promotions on each operand
       independently; unlike arithmetic compound assignments they do not use the
       usual arithmetic conversions to a shared operand type. */
    if (expression->kind == MINIC_EXPRESSION_COMPOUND_ASSIGNMENT &&
        (expression->value.binary.operator_kind == MINIC_BINARY_ADD ||
         expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT ||
         expression->value.binary.operator_kind == MINIC_BINARY_SHIFT_LEFT ||
         expression->value.binary.operator_kind == MINIC_BINARY_SHIFT_RIGHT ||
         expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_AND ||
         expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_XOR ||
         expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_OR)) {
        const MinicExpression *source;
        const MinicExpression *target;
        MinicCoreObjectId address_object;
        MinicCoreObjectId current_object;
        MinicCoreValueId address;
        MinicCoreValueId current;
        MinicCoreValueId current_common;
        MinicCoreValueId right;
        MinicCoreValueId right_common;
        MinicCoreValueId result;
        MinicCoreValueId stored_value;
        MinicCoreLowerStatus status;
        MinicType address_type;
        MinicType common_type;
        MinicType right_type;
        MinicType stored_type;
        bool shift_assignment;

        target = minic_c0_program_expression(context->body->program, expression->value.binary.left);
        source =
            minic_c0_program_expression(context->body->program, expression->value.binary.right);
        if (target == NULL || source == NULL || target->value_category != MINIC_VALUE_LVALUE ||
            !minic_type_equal(expression->type, target->type) ||
            minic_type_is_const(target->type) ||
            !minic_type_unqualified(target->type, &stored_type) ||
            !minic_type_is_integer(stored_type) || !minic_type_is_integer(source->type) ||
            context->target == NULL) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        shift_assignment =
            expression->value.binary.operator_kind == MINIC_BINARY_SHIFT_LEFT ||
            expression->value.binary.operator_kind == MINIC_BINARY_SHIFT_RIGHT;
        if (shift_assignment) {
            if (!minic_target_info_integer_promotion_for_program(
                    context->target, context->body->program, stored_type, &common_type) ||
                !minic_target_info_integer_promotion_for_program(
                    context->target, context->body->program, source->type, &right_type)) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
        } else {
            if (!minic_target_info_integer_common_for_program(context->target,
                                                              context->body->program,
                                                              stored_type,
                                                              source->type,
                                                              &common_type)) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            right_type = common_type;
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
        if (address >= context->function->value_count) {
            return MINIC_CORE_LOWER_ERROR;
        }
        address_type = context->function->values[address].type;
        status = spill_scalar_value(context, target->span, address_type, address, &address_object);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status =
            spill_scalar_value(context, target->span, common_type, current_common, &current_object);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = lower_expression(context, expression->value.binary.right, &right);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status =
            append_integer_conversion(context, source->span, right_type, right, &right_common);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = reload_scalar_value(
            context, target->span, common_type, current_object, &current_common);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = reload_scalar_value(context, target->span, address_type, address_object, &address);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        switch (expression->value.binary.operator_kind) {
        case MINIC_BINARY_ADD:
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_ADD;
            break;
        case MINIC_BINARY_SUBTRACT:
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT;
            break;
        case MINIC_BINARY_SHIFT_LEFT:
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT;
            break;
        case MINIC_BINARY_SHIFT_RIGHT:
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT;
            break;
        case MINIC_BINARY_BITWISE_AND:
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND;
            break;
        case MINIC_BINARY_BITWISE_XOR:
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR;
            break;
        case MINIC_BINARY_BITWISE_OR:
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR;
            break;
        default:
            return MINIC_CORE_LOWER_ERROR;
        }
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
    MinicCoreObjectId stored_object;
    MinicCoreValueId address_id;
    MinicCoreValueId stored_value;
    MinicCoreLowerStatus status;
    MinicType stored_type;

    if (context == NULL || context->body == NULL || context->body->program == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    target = minic_c0_program_expression(context->body->program, target_id);
    if (target == NULL || target->value_category != MINIC_VALUE_LVALUE) {
        return MINIC_CORE_LOWER_ERROR;
    }
    if (!minic_type_unqualified(target->type, &stored_type) ||
        !core_memory_scalar_type(stored_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    status = lower_scalar_assignment_value(context, stored_type, source_id, &stored_value);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    status = spill_scalar_value(context, span, stored_type, stored_value, &stored_object);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    status = lower_address(context, target_id, &address_id);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    status = reload_scalar_value(context, span, stored_type, stored_object, &stored_value);
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

static MinicCoreLowerStatus lower_scalar_update(MinicCoreLowerContext *context,
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
    *value_id = prefix ? updated : current;
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
    /* M54_VOID_CONDITIONAL_STATEMENT: expression statements are only an
       effect boundary. Once M53 can lower a void conditional expression, the
       statement layer must delegate rather than reject the expression kind. */
    if (expression->kind == MINIC_EXPRESSION_CONDITIONAL &&
        minic_type_is_void(expression->type)) {
        MinicCoreValueId discarded_value;
        MinicCoreLowerStatus status;

        status = lower_expression(context, statement->expression, &discarded_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        return discarded_value == MINIC_CORE_VALUE_INVALID ? MINIC_CORE_LOWER_OK
                                                            : MINIC_CORE_LOWER_ERROR;
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
         expression->value.unary.operator_kind == MINIC_UNARY_POST_DECREMENT ||
         expression->value.unary.operator_kind == MINIC_UNARY_PRE_INCREMENT ||
         expression->value.unary.operator_kind == MINIC_UNARY_PRE_DECREMENT)) {
        MinicCoreValueId discarded_value;

        return lower_scalar_update(context, expression, &discarded_value);
    }
    if (expression->kind != MINIC_EXPRESSION_ASSIGNMENT) {
        MinicCoreValueId discarded_value;
        MinicType discarded_type;

        if (expression->kind == MINIC_EXPRESSION_STATEMENT &&
            expression->value.statement_expression.result == MINIC_EXPRESSION_INVALID &&
            minic_type_is_void(expression->type)) {
            return lower_expression(context, statement->expression, &discarded_value);
        }
        if (!core_scalar_expression_value_type(context->body, expression, &discarded_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        (void)discarded_type;
        return lower_expression(context, statement->expression, &discarded_value);
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
    terminator.return_object = MINIC_CORE_OBJECT_INVALID;
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
        } else if (minic_type_is_record(context->source_function->return_type)) {
            const MinicExpression *expression;
            const MinicLocal *local;
            MinicType value_type;

            expression = minic_c0_program_expression(context->body->program, statement->expression);
            if (expression == NULL || expression->kind != MINIC_EXPRESSION_LOCAL ||
                expression->value_category != MINIC_VALUE_LVALUE ||
                !minic_type_unqualified(expression->type, &value_type) ||
                !minic_type_equal(value_type, context->source_function->return_type)) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            local = minic_c0_program_local(context->body->program, expression->value.local_id);
            if (local == NULL || !minic_type_is_record(local->type)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            status =
                lower_local_object(context, expression->value.local_id, &terminator.return_object);
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

static bool core_inline_asm_constraint_is(const MinicInlineAsmOperand *operand,
                                              const char *text) {
    size_t length;

    if (operand == NULL || text == NULL || operand->constraint_text == NULL) {
        return false;
    }
    length = strlen(text);
    return operand->constraint_length == length &&
           memcmp(operand->constraint_text, text, length) == 0;
}

static bool core_inline_asm_register_output_constraint(const MinicInlineAsmOperand *operand) {
    return core_inline_asm_constraint_is(operand, "=r") ||
           core_inline_asm_constraint_is(operand, "=&r");
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
        return minic_core_function_append_effect_instruction(
                   context->function, context->block_id, &instruction)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }

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
                    return MINIC_CORE_LOWER_UNSUPPORTED;
                }
                memory_output = candidate;
                memory_index = output_index;
            } else if (candidate->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY &&
                       core_inline_asm_register_output_constraint(candidate)) {
                if (register_output != NULL) {
                    return MINIC_CORE_LOWER_UNSUPPORTED;
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
                return MINIC_CORE_LOWER_ERROR;
            }
            if (!register_local->is_array &&
                minic_c0_program_local_fixed_register_binding(
                    context->body->program, register_expression->value.local_id) == NULL &&
                minic_type_equal(register_local->type, register_expression->type)) {
                status = lower_expression(context, input->expression, &input_value);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
                status = lower_address(context, memory_output->expression, &memory_address);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
                if (input_value >= context->function->value_count ||
                    memory_address >= context->function->value_count ||
                    !minic_type_equal(context->function->values[input_value].type, input_type)) {
                    return MINIC_CORE_LOWER_ERROR;
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
                    return MINIC_CORE_LOWER_ERROR;
                }
                status = lower_address(context, register_output->expression, &output_address);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
                (void)memset(&instruction, 0, sizeof(instruction));
                instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
                instruction.span = statement->span;
                instruction.type = minic_type_void();
                instruction.result = MINIC_CORE_VALUE_INVALID;
                instruction.value.store.address = output_address;
                instruction.value.store.stored_value = output_value;
                instruction.value.store.is_volatile = false;
                return minic_core_function_append_effect_instruction(
                           context->function, context->block_id, &instruction)
                           ? MINIC_CORE_LOWER_OK
                           : MINIC_CORE_LOWER_ERROR;
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
                return status;
            }
            status = lower_address(context, memory_output->expression, &memory_address);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            if (input_value >= context->function->value_count ||
                memory_address >= context->function->value_count ||
                !minic_type_equal(context->function->values[input_value].type, input_type)) {
                return MINIC_CORE_LOWER_ERROR;
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
            return minic_core_function_append_effect_instruction(
                       context->function, context->block_id, &instruction)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
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
                return MINIC_CORE_LOWER_ERROR;
            }
            if (!local->is_array &&
                minic_c0_program_local_fixed_register_binding(
                    context->body->program, output_expression->value.local_id) == NULL &&
                minic_type_equal(local->type, output_expression->type)) {
                status = lower_expression(context, input->expression, &input_value);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
                if (input_value >= context->function->value_count ||
                    !minic_type_equal(context->function->values[input_value].type, input_type)) {
                    return MINIC_CORE_LOWER_ERROR;
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
                instruction.kind = MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INPUT_INLINE_ASM;
                instruction.span = statement->span;
                instruction.type = output_type;
                instruction.result = MINIC_CORE_VALUE_INVALID;
                instruction.value.register_output_input_inline_asm.inline_asm_id = inline_asm_id;
                instruction.value.register_output_input_inline_asm.operand = input_value;
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
                return MINIC_CORE_LOWER_ERROR;
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
    }

    if (!source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length == 0U && source->outputs != NULL && source->output_count == 1U &&
        source->input_count == 0U && source->label_count == 0U && source->clobber_count == 0U &&
        source->register_clobber_count == 0U && !source->has_memory_clobber) {
        const MinicInlineAsmOperand *output;
        const MinicExpression *output_expression;
        const MinicLocal *local;

        output = &source->outputs[0];
        output_expression = minic_c0_program_expression(context->body->program, output->expression);
        if (output->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE &&
            output->constraint_text != NULL && output->constraint_length == 3U &&
            memcmp(output->constraint_text, "+rm", 3U) == 0 && output_expression != NULL &&
            output_expression->kind == MINIC_EXPRESSION_LOCAL &&
            output_expression->value_category == MINIC_VALUE_LVALUE &&
            core_memory_scalar_type(output_expression->type) &&
            !minic_type_is_const(output_expression->type) &&
            !minic_type_is_volatile(output_expression->type)) {
            local =
                minic_c0_program_local(context->body->program, output_expression->value.local_id);
            if (local == NULL) {
                return MINIC_CORE_LOWER_ERROR;
            }
            if (!local->is_array && !local->is_register_storage &&
                minic_type_equal(local->type, output_expression->type) &&
                !minic_type_is_const(local->type) && !minic_type_is_volatile(local->type)) {
                return MINIC_CORE_LOWER_OK;
            }
        }
    }

    /* M59_EMPTY_SCALAR_INPUT_BARRIER: GNU barrier_data() is an empty
       volatile asm with one scalar register input and a memory clobber. The
       operand must still be evaluated, but an empty target template needs no
       target instruction. Represent the ordering effect with the existing
       target-neutral compiler barrier rather than inventing an empty opaque
       asm encoding. */
    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length == 0U && source->output_count == 0U && source->inputs != NULL &&
        source->input_count == 1U && source->label_count == 0U &&
        source->register_clobber_count == 0U && source->has_memory_clobber &&
        source->clobber_count == 1U) {
        const MinicInlineAsmOperand *input;
        const MinicExpression *input_expression;
        MinicCoreValueId discarded_input;
        MinicCoreLowerStatus input_status;
        MinicType input_type;

        input = &source->inputs[0];
        input_expression = minic_c0_program_expression(context->body->program, input->expression);
        if (input->access == MINIC_INLINE_ASM_OPERAND_READ_ONLY &&
            (core_inline_asm_constraint_is(input, "r") ||
             core_inline_asm_constraint_is(input, "rK")) &&
            input_expression != NULL &&
            core_scalar_expression_value_type(context->body, input_expression, &input_type)) {
            input_status = lower_expression(context, input->expression, &discarded_input);
            if (input_status != MINIC_CORE_LOWER_OK) {
                return input_status;
            }
            if (discarded_input >= context->function->value_count ||
                !minic_type_equal(context->function->values[discarded_input].type, input_type)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_COMPILER_BARRIER;
            instruction.span = statement->span;
            instruction.type = minic_type_void();
            instruction.result = MINIC_CORE_VALUE_INVALID;
            return minic_core_function_append_effect_instruction(
                       context->function, context->block_id, &instruction)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
    }

    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
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
                return input_status;
            }
            if (input_value >= context->function->value_count ||
                !minic_type_equal(context->function->values[input_value].type, input_type)) {
                return MINIC_CORE_LOWER_ERROR;
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
            instruction.kind = MINIC_CORE_INSTRUCTION_SCALAR_INPUT_INLINE_ASM;
            instruction.span = statement->span;
            instruction.type = minic_type_void();
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.scalar_input_inline_asm.inline_asm_id = inline_asm_id;
            instruction.value.scalar_input_inline_asm.operand = input_value;
            return minic_core_function_append_effect_instruction(
                       context->function, context->block_id, &instruction)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
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

#define MINIC_CORE_SWITCH_LABEL_LIMIT 128U

typedef struct MinicCoreSwitchLabel {
    size_t source_index;
    const MinicStatement *statement;
    MinicCoreBlockId body_block;
    MinicCoreBlockId test_block;
} MinicCoreSwitchLabel;

static MinicCoreLowerStatus append_switch_integer_constant(MinicCoreLowerContext *context,
                                                           MinicSourceSpan span,
                                                           MinicType type,
                                                           int64_t value,
                                                           MinicCoreValueId *value_id) {
    MinicCoreInstruction instruction;

    if (context == NULL || context->function == NULL || value_id == NULL ||
        !minic_type_is_integer(type)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
    instruction.span = span;
    instruction.type = type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.integer_value = value;
    return minic_core_function_append_value_instruction(
               context->function, context->block_id, &instruction, value_id)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

static MinicCoreLowerStatus set_switch_conditional_branch(MinicCoreLowerContext *context,
                                                          MinicSourceSpan span,
                                                          MinicCoreValueId condition,
                                                          MinicCoreBlockId when_true,
                                                          MinicCoreBlockId when_false) {
    MinicCoreTerminator terminator;

    if (context == NULL || context->function == NULL ||
        condition >= context->function->value_count ||
        !minic_type_is_integer(context->function->values[condition].type) ||
        when_true == MINIC_CORE_BLOCK_INVALID || when_false == MINIC_CORE_BLOCK_INVALID) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&terminator, 0, sizeof(terminator));
    terminator.kind = MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH;
    terminator.span = span;
    terminator.return_value = MINIC_CORE_VALUE_INVALID;
    terminator.conditional.condition = condition;
    terminator.conditional.when_true = when_true;
    terminator.conditional.when_false = when_false;
    return minic_core_function_set_terminator(context->function, context->block_id, &terminator)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

static MinicCoreLowerStatus lower_switch_case_dispatch(MinicCoreLowerContext *context,
                                                       const MinicStatement *case_statement,
                                                       MinicType selector_type,
                                                       MinicCoreObjectId selector_object,
                                                       MinicCoreBlockId body_target,
                                                       MinicCoreBlockId next_target) {
    const MinicExpression *lower_expression;
    const MinicExpression *upper_expression;
    MinicCoreInstruction instruction;
    MinicCoreValueId bound;
    MinicCoreValueId comparison;
    MinicCoreValueId selector;
    MinicCoreLowerStatus status;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || case_statement == NULL ||
        case_statement->kind != MINIC_STATEMENT_CASE ||
        case_statement->expression == MINIC_EXPRESSION_INVALID ||
        !minic_type_is_integer(selector_type) || body_target == MINIC_CORE_BLOCK_INVALID ||
        next_target == MINIC_CORE_BLOCK_INVALID) {
        return MINIC_CORE_LOWER_ERROR;
    }
    lower_expression =
        minic_c0_program_expression(context->body->program, case_statement->expression);
    if (lower_expression == NULL || lower_expression->kind != MINIC_EXPRESSION_INTEGER ||
        !minic_type_is_integer(lower_expression->type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }

    status = reload_scalar_value(
        context, case_statement->span, selector_type, selector_object, &selector);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    status = append_switch_integer_constant(context,
                                            lower_expression->span,
                                            selector_type,
                                            lower_expression->value.integer_value,
                                            &bound);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }

    if (case_statement->target_expression == MINIC_EXPRESSION_INVALID) {
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_SCALAR_EQUAL;
        instruction.span = case_statement->span;
        instruction.type = minic_type_int();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = selector;
        instruction.value.binary.right = bound;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &comparison)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        return set_switch_conditional_branch(
            context, case_statement->span, comparison, body_target, next_target);
    }

    upper_expression =
        minic_c0_program_expression(context->body->program, case_statement->target_expression);
    if (upper_expression == NULL || upper_expression->kind != MINIC_EXPRESSION_INTEGER ||
        !minic_type_is_integer(upper_expression->type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    {
        MinicCoreBlockId upper_test_block;

        if (!minic_core_function_add_block(context->function, &upper_test_block)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_LESS;
        instruction.span = case_statement->span;
        instruction.type = minic_type_int();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = selector;
        instruction.value.binary.right = bound;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &comparison)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = set_switch_conditional_branch(
            context, case_statement->span, comparison, next_target, upper_test_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }

        context->block_id = upper_test_block;
        status = append_switch_integer_constant(context,
                                                upper_expression->span,
                                                selector_type,
                                                upper_expression->value.integer_value,
                                                &bound);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = reload_scalar_value(
            context, case_statement->span, selector_type, selector_object, &selector);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_LESS;
        instruction.span = case_statement->span;
        instruction.type = minic_type_int();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = bound;
        instruction.value.binary.right = selector;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &comparison)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        return set_switch_conditional_branch(
            context, case_statement->span, comparison, next_target, body_target);
    }
}

static MinicCoreLowerStatus
lower_switch(MinicCoreLowerContext *context, const MinicStatement *statement, bool *terminated) {
    const MinicBlock *body;
    const MinicExpression *selector_expression;
    MinicCoreSwitchLabel labels[MINIC_CORE_SWITCH_LABEL_LIMIT];
    MinicCoreBlockId default_target;
    MinicCoreBlockId dispatch_target;
    MinicCoreBlockId exit_block;
    MinicCoreObjectId selector_object;
    MinicCoreValueId selector_normalized;
    MinicCoreValueId selector_source;
    MinicCoreLowerStatus status;
    MinicType selector_type;
    size_t case_count;
    size_t default_label;
    size_t first_case_label;
    size_t label_count;
    size_t source_index;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || statement == NULL || terminated == NULL ||
        statement->kind != MINIC_STATEMENT_SWITCH ||
        statement->cleanup_context != MINIC_CLEANUP_CONTEXT_ROOT ||
        statement->cleanup_stop_context != MINIC_CLEANUP_CONTEXT_ROOT ||
        statement->expression == MINIC_EXPRESSION_INVALID ||
        statement->then_block == MINIC_BLOCK_INVALID ||
        statement->else_block != MINIC_BLOCK_INVALID || context->target == NULL) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    selector_expression =
        minic_c0_program_expression(context->body->program, statement->expression);
    body = minic_c0_program_block(context->body->program, statement->then_block);
    if (selector_expression == NULL || body == NULL ||
        !minic_type_is_integer(selector_expression->type) ||
        !minic_target_info_integer_promotion_for_program(
            context->target, context->body->program, selector_expression->type, &selector_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }

    case_count = 0U;
    default_label = SIZE_MAX;
    first_case_label = SIZE_MAX;
    label_count = 0U;
    for (source_index = 0U; source_index < body->statement_count; ++source_index) {
        const MinicStatement *source_statement;

        source_statement =
            minic_c0_program_statement(context->body->program, body->statements[source_index]);
        if (source_statement == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (source_statement->kind != MINIC_STATEMENT_CASE &&
            source_statement->kind != MINIC_STATEMENT_DEFAULT) {
            if (label_count == 0U) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            continue;
        }
        if (source_statement->cleanup_context != MINIC_CLEANUP_CONTEXT_ROOT ||
            source_statement->cleanup_stop_context != MINIC_CLEANUP_CONTEXT_ROOT ||
            source_statement->then_block != MINIC_BLOCK_INVALID ||
            source_statement->else_block != MINIC_BLOCK_INVALID ||
            label_count >= MINIC_CORE_SWITCH_LABEL_LIMIT) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        labels[label_count].source_index = source_index;
        labels[label_count].statement = source_statement;
        labels[label_count].body_block = MINIC_CORE_BLOCK_INVALID;
        labels[label_count].test_block = MINIC_CORE_BLOCK_INVALID;
        if (source_statement->kind == MINIC_STATEMENT_CASE) {
            if (source_statement->expression == MINIC_EXPRESSION_INVALID) {
                return MINIC_CORE_LOWER_ERROR;
            }
            if (first_case_label == SIZE_MAX) {
                first_case_label = label_count;
            }
            case_count += 1U;
        } else {
            if (default_label != SIZE_MAX ||
                source_statement->expression != MINIC_EXPRESSION_INVALID ||
                source_statement->target_expression != MINIC_EXPRESSION_INVALID) {
                return MINIC_CORE_LOWER_ERROR;
            }
            default_label = label_count;
        }
        label_count += 1U;
    }

    status = lower_expression(context, statement->expression, &selector_source);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    status = append_integer_conversion(
        context, selector_expression->span, selector_type, selector_source, &selector_normalized);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    status = spill_scalar_value(
        context, selector_expression->span, selector_type, selector_normalized, &selector_object);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }

    if (!minic_core_function_add_block(context->function, &exit_block)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    for (source_index = 0U; source_index < label_count; ++source_index) {
        if (!minic_core_function_add_block(context->function, &labels[source_index].body_block)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (labels[source_index].statement->kind == MINIC_STATEMENT_CASE &&
            !minic_core_function_add_block(context->function, &labels[source_index].test_block)) {
            return MINIC_CORE_LOWER_ERROR;
        }
    }

    default_target = default_label == SIZE_MAX ? exit_block : labels[default_label].body_block;
    dispatch_target =
        first_case_label == SIZE_MAX ? default_target : labels[first_case_label].test_block;
    status = set_branch(context, context->block_id, statement->span, dispatch_target);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }

    if (case_count != 0U) {
        size_t label_index;

        for (label_index = 0U; label_index < label_count; ++label_index) {
            size_t next_label;
            MinicCoreBlockId next_target;

            if (labels[label_index].statement->kind != MINIC_STATEMENT_CASE) {
                continue;
            }
            next_target = default_target;
            for (next_label = label_index + 1U; next_label < label_count; ++next_label) {
                if (labels[next_label].statement->kind == MINIC_STATEMENT_CASE) {
                    next_target = labels[next_label].test_block;
                    break;
                }
            }
            context->block_id = labels[label_index].test_block;
            status = lower_switch_case_dispatch(context,
                                                labels[label_index].statement,
                                                selector_type,
                                                selector_object,
                                                labels[label_index].body_block,
                                                next_target);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
        }
    }

    for (source_index = 0U; source_index < label_count; ++source_index) {
        MinicBlock segment;
        MinicCoreBlockId fallthrough_target;
        size_t break_index;
        size_t segment_begin;
        size_t segment_end;
        size_t scan;
        bool segment_terminated;

        segment_begin = labels[source_index].source_index + 1U;
        segment_end = source_index + 1U < label_count ? labels[source_index + 1U].source_index
                                                      : body->statement_count;
        break_index = SIZE_MAX;
        for (scan = segment_begin; scan < segment_end; ++scan) {
            const MinicStatement *segment_statement;

            segment_statement =
                minic_c0_program_statement(context->body->program, body->statements[scan]);
            if (segment_statement == NULL) {
                return MINIC_CORE_LOWER_ERROR;
            }
            if (segment_statement->kind == MINIC_STATEMENT_BREAK) {
                if (break_index != SIZE_MAX || scan + 1U != segment_end ||
                    segment_statement->cleanup_context != MINIC_CLEANUP_CONTEXT_ROOT ||
                    segment_statement->cleanup_stop_context != MINIC_CLEANUP_CONTEXT_ROOT) {
                    return MINIC_CORE_LOWER_UNSUPPORTED;
                }
                break_index = scan;
            }
        }

        context->block_id = labels[source_index].body_block;
        segment_terminated = false;
        segment = *body;
        segment.statements = body->statements + segment_begin;
        segment.statement_count =
            (break_index == SIZE_MAX ? segment_end : break_index) - segment_begin;
        segment.statement_capacity = segment.statement_count;
        if (segment.statement_count != 0U) {
            status = lower_block(context, &segment, &segment_terminated);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
        }
        if (segment_terminated) {
            continue;
        }
        if (break_index != SIZE_MAX) {
            fallthrough_target = exit_block;
        } else if (source_index + 1U < label_count) {
            fallthrough_target = labels[source_index + 1U].body_block;
        } else {
            fallthrough_target = exit_block;
        }
        status = set_branch(context, context->block_id, statement->span, fallthrough_target);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
    }

    context->block_id = exit_block;
    *terminated = false;
    return MINIC_CORE_LOWER_OK;
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
            case MINIC_STATEMENT_SWITCH:
                status = lower_switch(context, statement, &statement_terminated);
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
