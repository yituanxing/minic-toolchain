#include "compiler/c_lower/core_lower_internal.h"

/* Core-owned semantic metadata capture stays separate from expression/control lowering. */
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
               context->source, type.enum_id, entity->compatible_type);
}

bool core_capture_enum_metadata(MinicCoreLowerContext *context) {
    MinicCoreFunction *function;
    const MinicCoreSourceDraft *source;
    size_t index;
    size_t parameter_index;

    if (context == NULL || context->function == NULL) {
        return false;
    }
    function = context->function;
    source = context->source != NULL ? context->source->source_draft : NULL;
    if (source == NULL ||
        source->global_type_count != function->global_count ||
        source->callee_count != function->callee_count ||
        source->call_signature_count != function->call_signature_count ||
        source->object_type_count != function->object_count ||
        source->instruction_type_count != function->instruction_count ||
        (function->parameter_count != 0U && source->parameter_types == NULL)) {
        return false;
    }
    if (!core_capture_enum_type_metadata(context, source->return_type)) {
        return false;
    }
    for (index = 0U; index < function->parameter_count; ++index) {
        if (!core_capture_enum_type_metadata(context, source->parameter_types[index])) {
            return false;
        }
    }
    for (index = 0U; index < function->global_count; ++index) {
        if (!core_capture_enum_type_metadata(context, source->global_types[index])) {
            return false;
        }
    }
    for (index = 0U; index < function->callee_count; ++index) {
        const MinicCoreSourceCallType *source_callee = &source->callees[index];

        if (!core_capture_enum_type_metadata(context, source_callee->return_type)) {
            return false;
        }
        for (parameter_index = 0U; parameter_index < source_callee->parameter_count; ++parameter_index) {
            if (!core_capture_enum_type_metadata(context, source_callee->parameter_types[parameter_index])) {
                return false;
            }
        }
    }
    for (index = 0U; index < function->call_signature_count; ++index) {
        const MinicCoreSourceCallType *source_signature = &source->call_signatures[index];

        if (!core_capture_enum_type_metadata(context, source_signature->return_type)) {
            return false;
        }
        for (parameter_index = 0U; parameter_index < source_signature->parameter_count;
             ++parameter_index) {
            if (!core_capture_enum_type_metadata(
                    context, source_signature->parameter_types[parameter_index])) {
                return false;
            }
        }
    }
    for (index = 0U; index < function->object_count; ++index) {
        if (!core_capture_enum_type_metadata(context, source->object_types[index])) {
            return false;
        }
    }
    for (index = 0U; index < function->value_count; ++index) {
        if (!core_capture_enum_type_metadata(
                context,
                minic_core_function_source_value_type(context->source, (MinicCoreValueId)index))) {
            return false;
        }
    }
    for (index = 0U; index < function->instruction_count; ++index) {
        if (!core_capture_enum_type_metadata(context, source->instruction_types[index])) {
            return false;
        }
    }
    return true;
}

bool core_import_fixed_register_binding(MinicCoreLowerContext *context,
                                               size_t source_binding_id,
                                               size_t *core_binding_id) {
    const MinicFixedRegisterBinding *binding;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || core_binding_id == NULL) {
        return false;
    }
    binding =
        minic_c0_program_fixed_register_binding(context->body->program, source_binding_id);
    return binding != NULL && binding->register_name != NULL &&
           binding->register_name_length != 0U && core_memory_scalar_type(binding->type) &&
           minic_core_function_add_fixed_register_binding(context->source,
                                                          binding->register_name,
                                                          binding->register_name_length,
                                                          binding->type,
                                                          binding->is_local,
                                                          core_binding_id);
}
