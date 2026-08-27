#include "core/core_lower_internal.h"

static bool core_capture_enum_type_metadata(MinicCoreLowerContext *context, MinicType type) {
    const MinicEnum *entity;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL) {
        return false;
    }
    if (type.base_kind != MINIC_TYPE_BASE_ENUM || type.enum_id == MINIC_ENUM_INVALID) {
        return true;
    }
    entity = minic_c0_program_enum(context->body->program, type.enum_id);
    if (entity == NULL) {
        return false;
    }
    /* Pointers to an incomplete enum may legitimately reach Core as addresses;
       only complete enum values need an execution representation snapshot. */
    if (!entity->is_complete) {
        return true;
    }
    return minic_type_is_integer(entity->compatible_type) &&
           !minic_type_is_enum(entity->compatible_type) &&
           !minic_type_is_pointer(entity->compatible_type) &&
           minic_core_function_add_enum_type(
               context->function, type.enum_id, entity->compatible_type);
}

bool core_capture_enum_metadata(MinicCoreLowerContext *context) {
    MinicCoreFunction *function;
    size_t index;
    size_t parameter_index;

    if (context == NULL || context->function == NULL) {
        return false;
    }
    function = context->function;
    if (!core_capture_enum_type_metadata(context, function->return_type)) {
        return false;
    }
    for (index = 0U; index < function->parameter_count; ++index) {
        if (!core_capture_enum_type_metadata(context, function->parameter_types[index])) {
            return false;
        }
    }
    for (index = 0U; index < function->global_count; ++index) {
        if (!core_capture_enum_type_metadata(context, function->globals[index].type)) {
            return false;
        }
    }
    for (index = 0U; index < function->callee_count; ++index) {
        const MinicCoreCallee *callee = &function->callees[index];

        if (!core_capture_enum_type_metadata(context, callee->return_type)) {
            return false;
        }
        for (parameter_index = 0U; parameter_index < callee->parameter_count; ++parameter_index) {
            if (!core_capture_enum_type_metadata(context, callee->parameter_types[parameter_index])) {
                return false;
            }
        }
    }
    for (index = 0U; index < function->call_signature_count; ++index) {
        const MinicCoreCallSignature *signature = &function->call_signatures[index];

        if (!core_capture_enum_type_metadata(context, signature->return_type)) {
            return false;
        }
        for (parameter_index = 0U; parameter_index < signature->parameter_count;
             ++parameter_index) {
            if (!core_capture_enum_type_metadata(
                    context, signature->parameter_types[parameter_index])) {
                return false;
            }
        }
    }
    for (index = 0U; index < function->object_count; ++index) {
        if (!core_capture_enum_type_metadata(context, function->objects[index].type)) {
            return false;
        }
    }
    for (index = 0U; index < function->value_count; ++index) {
        if (!core_capture_enum_type_metadata(context, function->values[index].type)) {
            return false;
        }
    }
    for (index = 0U; index < function->instruction_count; ++index) {
        if (!core_capture_enum_type_metadata(context, function->instructions[index].type)) {
            return false;
        }
    }
    return true;
}
