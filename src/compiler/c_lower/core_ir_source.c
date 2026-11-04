#include "compiler/c_lower/core_ir_source_internal.h"
#include "core/core_ir_storage.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

/* C-language source admission, mutable pre-snapshot descriptors, and their
 * transactional conversion to source-independent CoreType snapshots. Only
 * this file knows MinicType; pure Core construction/verification does not. */

bool minic_core_scalar_bitcast_types_valid(MinicType target_type, MinicType source_type) {
    return (minic_type_is_pointer(target_type) &&
            (minic_type_is_pointer(source_type) || minic_type_is_integer(source_type))) ||
           (minic_type_is_integer(target_type) && minic_type_is_pointer(source_type));
}

static void minic_core_source_draft_release(MinicCoreSourceDraft *draft) {
    size_t index;

    if (draft == NULL) return;
    free(draft->parameter_types);
    free(draft->enum_types);
    free(draft->fixed_register_binding_types);
    free(draft->object_types);
    free(draft->global_types);
    for (index = 0U; draft->callees != NULL && index < draft->callee_count; ++index)
        free(draft->callees[index].parameter_types);
    free(draft->callees);
    for (index = 0U; draft->call_signatures != NULL &&
                     index < draft->call_signature_count; ++index)
        free(draft->call_signatures[index].parameter_types);
    free(draft->call_signatures);
    free(draft->instruction_types);
    free(draft);
}

void minic_core_source_builder_initialize(MinicCoreSourceBuilder *builder) {
    if (builder == NULL) return;
    (void)memset(builder, 0, sizeof(*builder));
    minic_core_function_initialize(&builder->core);
}

void minic_core_source_builder_destroy(MinicCoreSourceBuilder *builder) {
    if (builder == NULL) return;
    minic_core_source_draft_release(builder->source_draft);
    builder->source_draft = NULL;
    minic_core_function_destroy(&builder->core);
}

bool minic_core_source_builder_take_core(MinicCoreSourceBuilder *builder,
                                         MinicCoreFunction *output) {
    if (builder == NULL || output == NULL || output == &builder->core ||
        builder->source_draft != NULL || builder->core.type_context == NULL ||
        !minic_core_function_verify_core_type_snapshot(&builder->core))
        return false;
    minic_core_function_destroy(output);
    *output = builder->core;
    minic_core_function_initialize(&builder->core);
    return true;
}

bool minic_core_function_set_signature(MinicCoreSourceBuilder *builder,
                                       const char *name,
                                       size_t name_length,
                                       MinicType return_type,
                                       const MinicType *parameter_types,
                                       size_t parameter_count) {
    MinicCoreFunction *function = builder == NULL ? NULL : &builder->core;
    char *name_copy;
    MinicType *parameters_copy;
    MinicCoreSourceDraft *draft;

    if (function == NULL || function->name != NULL || builder->source_draft != NULL ||
        function->parameter_count != 0U || name == NULL || name_length == 0U ||
        (parameter_count != 0U && parameter_types == NULL) ||
        parameter_count > SIZE_MAX / sizeof(*parameters_copy)) {
        return false;
    }
    name_copy = minic_core_ir_copy_name(name, name_length);
    if (name_copy == NULL) {
        return false;
    }
    draft = (MinicCoreSourceDraft *)calloc(1U, sizeof(*draft));
    if (draft == NULL) {
        free(name_copy);
        return false;
    }
    parameters_copy = NULL;
    if (parameter_count != 0U) {
        parameters_copy = (MinicType *)malloc(parameter_count * sizeof(*parameters_copy));
        if (parameters_copy == NULL) {
            free(name_copy);
            free(draft);
            return false;
        }
        (void)memcpy(parameters_copy, parameter_types, parameter_count * sizeof(*parameters_copy));
    }
    function->name = name_copy;
    function->name_length = name_length;
    draft->return_type = return_type;
    draft->parameter_types = parameters_copy;
    builder->source_draft = draft;
    function->parameter_count = parameter_count;
    return true;
}

bool minic_core_function_add_enum_type(MinicCoreSourceBuilder *builder,
                                       MinicEnumId enum_id,
                                       MinicType effective_integer_type) {
    MinicCoreFunction *function = builder == NULL ? NULL : &builder->core;
    MinicCoreSourceDraft *draft;
    size_t index;

    if (function == NULL || function->type_context != NULL ||
        (draft = builder->source_draft) == NULL || enum_id == MINIC_ENUM_INVALID ||
        !minic_type_is_integer(effective_integer_type) ||
        minic_type_is_enum(effective_integer_type) ||
        minic_type_is_pointer(effective_integer_type)) {
        return false;
    }
    for (index = 0U; index < draft->enum_type_count; ++index) {
        const MinicCoreEnumType *existing = &draft->enum_types[index];

        if (existing->enum_id == enum_id) {
            return minic_type_equal(existing->effective_integer_type, effective_integer_type);
        }
    }
    if (!minic_core_ir_grow_array((void **)&draft->enum_types,
                    &draft->enum_type_capacity,
                    draft->enum_type_count,
                    sizeof(*draft->enum_types))) {
        return false;
    }
    draft->enum_types[draft->enum_type_count].enum_id = enum_id;
    draft->enum_types[draft->enum_type_count].effective_integer_type =
        effective_integer_type;
    draft->enum_type_count += 1U;
    return true;
}

bool minic_core_function_effective_integer_type(const MinicCoreSourceBuilder *builder,
                                                MinicType type,
                                                MinicType *effective_type) {
    const MinicCoreFunction *function = builder == NULL ? NULL : &builder->core;
    const MinicCoreSourceDraft *draft;
    size_t index;

    if (function == NULL || function->type_context != NULL ||
        (draft = builder->source_draft) == NULL || effective_type == NULL ||
        !minic_type_is_integer(type) ||
        minic_type_is_pointer(type)) {
        return false;
    }
    if (!minic_type_is_enum(type)) {
        *effective_type = type;
        return true;
    }
    for (index = 0U; index < draft->enum_type_count; ++index) {
        if (draft->enum_types[index].enum_id == type.enum_id) {
            *effective_type = draft->enum_types[index].effective_integer_type;
            effective_type->base_qualifiers = type.base_qualifiers;
            effective_type->explicit_alignment = type.explicit_alignment;
            return true;
        }
    }
    return false;
}

bool minic_core_function_add_fixed_register_binding(MinicCoreSourceBuilder *builder,
                                                    const char *register_name,
                                                    size_t register_name_length,
                                                    MinicType type,
                                                    bool is_local,
                                                    size_t *binding_id) {
    MinicCoreFunction *function = builder == NULL ? NULL : &builder->core;
    char *name_copy;
    MinicCoreSourceDraft *draft;
    size_t index;

    if (function == NULL || function->type_context != NULL ||
        (draft = builder->source_draft) == NULL ||
        register_name == NULL || register_name_length == 0U ||
        binding_id == NULL ||
        draft->fixed_register_binding_type_count != function->fixed_register_binding_count ||
        (!minic_type_is_integer(type) && !minic_type_is_pointer(type) &&
         !minic_type_is_double(type))) {
        return false;
    }
    for (index = 0U; index < function->fixed_register_binding_count; ++index) {
        const MinicCoreFixedRegisterBinding *existing =
            &function->fixed_register_bindings[index];

        if (existing->is_local == is_local &&
            minic_type_equal(draft->fixed_register_binding_types[index], type) &&
            existing->register_name_length == register_name_length &&
            memcmp(existing->register_name, register_name, register_name_length) == 0) {
            *binding_id = index;
            return true;
        }
    }
    if (!minic_core_ir_grow_array((void **)&function->fixed_register_bindings,
                    &function->fixed_register_binding_capacity,
                    function->fixed_register_binding_count,
                    sizeof(*function->fixed_register_bindings)) ||
        !minic_core_ir_grow_array((void **)&draft->fixed_register_binding_types,
                    &draft->fixed_register_binding_type_capacity,
                    draft->fixed_register_binding_type_count,
                    sizeof(*draft->fixed_register_binding_types))) {
        return false;
    }
    name_copy = minic_core_ir_copy_name(register_name, register_name_length);
    if (name_copy == NULL) {
        return false;
    }
    index = function->fixed_register_binding_count;
    function->fixed_register_bindings[index].register_name = name_copy;
    function->fixed_register_bindings[index].register_name_length = register_name_length;
    function->fixed_register_bindings[index].core_type_id = MINIC_CORE_TYPE_INVALID;
    function->fixed_register_bindings[index].is_local = is_local;
    draft->fixed_register_binding_types[index] = type;
    draft->fixed_register_binding_type_count += 1U;
    function->fixed_register_binding_count += 1U;
    *binding_id = index;
    return true;
}

bool minic_core_function_add_object(MinicCoreSourceBuilder *builder,
                                    MinicSourceSpan span,
                                    MinicType type,
                                    MinicCoreObjectId *object_id) {
    MinicCoreFunction *function = builder == NULL ? NULL : &builder->core;
    MinicCoreObjectId new_id;
    MinicCoreSourceDraft *draft;

    if (function == NULL || function->type_context != NULL ||
        (draft = builder->source_draft) == NULL || object_id == NULL ||
        draft->object_type_count != function->object_count ||
        function->object_count >= (size_t)UINT32_MAX ||
        minic_type_is_void(type) || minic_type_is_function(type) ||
        !minic_core_ir_grow_array((void **)&function->objects,
                    &function->object_capacity,
                    function->object_count,
                    sizeof(*function->objects)) ||
        !minic_core_ir_grow_array((void **)&draft->object_types,
                    &draft->object_type_capacity,
                    draft->object_type_count,
                    sizeof(*draft->object_types))) {
        return false;
    }
    new_id = (MinicCoreObjectId)function->object_count;
    function->objects[function->object_count].span = span;
    function->objects[function->object_count].core_type_id = MINIC_CORE_TYPE_INVALID;
    function->objects[function->object_count].explicit_alignment = 0U;
    draft->object_types[draft->object_type_count] = type;
    draft->object_type_count += 1U;
    function->object_count += 1U;
    *object_id = new_id;
    return true;
}

bool minic_core_ir_global_addressable_type(MinicType type) {
    return minic_type_is_integer(type) || minic_type_is_pointer(type) ||
           /* Floating globals remain addressable even when their arithmetic
            * is target-constrained. Linux's x87 probes consume float64
            * objects through GNU `m` operands without loading a C scalar;
            * the Core global-address contract must agree with the existing
            * shared address/aggregate lowering predicate. */
           minic_type_is_float(type) || minic_type_is_double(type) ||
           minic_type_is_long_double(type) ||
           minic_type_is_array(type) || minic_type_is_record(type) ||
           minic_type_is_void(type);
}

bool minic_core_function_add_global(MinicCoreSourceBuilder *builder,
                                    const char *name,
                                    size_t name_length,
                                    MinicType type,
                                    MinicCoreGlobalId *global_id) {
    MinicCoreFunction *function = builder == NULL ? NULL : &builder->core;
    char *name_copy;
    MinicCoreSourceDraft *draft;
    size_t index;

    if (function == NULL || function->type_context != NULL ||
        (draft = builder->source_draft) == NULL ||
        draft->global_type_count != function->global_count ||
        name == NULL || name_length == 0U || global_id == NULL ||
        function->global_count >= (size_t)UINT32_MAX ||
        !minic_core_ir_global_addressable_type(type)) {
        return false;
    }
    for (index = 0U; index < function->global_count; ++index) {
        const MinicCoreGlobal *existing;

        existing = &function->globals[index];
        if (existing->name_length == name_length &&
            memcmp(existing->name, name, name_length) == 0) {
            if (!minic_type_equal(draft->global_types[index], type)) {
                return false;
            }
            *global_id = (MinicCoreGlobalId)index;
            return true;
        }
    }
    name_copy = minic_core_ir_copy_name(name, name_length);
    if (name_copy == NULL || !minic_core_ir_grow_array((void **)&function->globals,
                                         &function->global_capacity,
                                         function->global_count,
                                         sizeof(*function->globals)) ||
        !minic_core_ir_grow_array((void **)&draft->global_types,
                    &draft->global_type_capacity,
                    draft->global_type_count,
                    sizeof(*draft->global_types))) {
        free(name_copy);
        return false;
    }
    function->globals[function->global_count].name = name_copy;
    function->globals[function->global_count].name_length = name_length;
    function->globals[function->global_count].core_type_id = MINIC_CORE_TYPE_INVALID;
    draft->global_types[draft->global_type_count] = type;
    draft->global_type_count += 1U;
    *global_id = (MinicCoreGlobalId)function->global_count;
    function->global_count += 1U;
    return true;
}

bool minic_core_ir_call_scalar_type(MinicType type) {
    /* Scalar call transport is an IR contract, not an arithmetic capability.
       Keep binary32 and binary64 on the same call seam; target ABI placement
       still decides the physical floating registers. */
    return minic_type_is_integer(type) || minic_type_is_pointer(type) ||
           minic_type_is_float(type) || minic_type_is_double(type) ||
           minic_type_is_long_double(type);
}

bool minic_core_ir_call_parameter_type(MinicType type) {
    return minic_core_ir_call_scalar_type(type) || minic_type_is_record(type);
}

bool minic_core_ir_direct_call_return_type(MinicType type) {
    return minic_type_is_void(type) || minic_core_ir_call_scalar_type(type) || minic_type_is_record(type);
}

static bool callee_signature_equal(const MinicCoreCallee *callee,
                                   const MinicCoreSourceCallType *source,
                                   MinicType return_type,
                                   const MinicType *parameter_types,
                                   size_t parameter_count,
                                   bool is_variadic,
                                   MinicFunctionCallingConvention calling_convention) {
    size_t index;

    /* The only caller already found this callee by exact name and length. */
    if (callee == NULL || source == NULL ||
        !minic_type_equal(source->return_type, return_type) ||
        source->parameter_count != parameter_count ||
        source->is_variadic != is_variadic ||
        source->calling_convention != calling_convention) {
        return false;
    }
    for (index = 0U; index < parameter_count; ++index) {
        if (!minic_type_equal(source->parameter_types[index], parameter_types[index])) {
            return false;
        }
    }
    return true;
}

bool minic_core_function_add_callee(MinicCoreSourceBuilder *builder,
                                    const char *name,
                                    size_t name_length,
                                    MinicType return_type,
                                    const MinicType *parameter_types,
                                    size_t parameter_count,
                                    bool is_variadic,
                                    MinicFunctionCallingConvention calling_convention,
                                    MinicCoreCalleeId *callee_id) {
    MinicCoreFunction *function = builder == NULL ? NULL : &builder->core;
    MinicCoreCallee stored;
    MinicCoreSourceCallType source_stored;
    MinicCoreSourceDraft *draft;
    size_t index;

    if (function == NULL || function->type_context != NULL ||
        (draft = builder->source_draft) == NULL ||
        draft->callee_count != function->callee_count ||
        name == NULL || name_length == 0U || callee_id == NULL ||
        function->callee_count >= (size_t)UINT32_MAX ||
        !minic_core_ir_direct_call_return_type(return_type) ||
        !minic_function_calling_convention_valid(calling_convention) ||
        (parameter_count != 0U && parameter_types == NULL) ||
        parameter_count > SIZE_MAX / sizeof(*source_stored.parameter_types)) {
        return false;
    }
    for (index = 0U; index < parameter_count; ++index) {
        if (!minic_core_ir_call_parameter_type(parameter_types[index])) {
            return false;
        }
    }
    /* The inline-ASM ingress validates its own records and the Core verifier
     * validates consumers. Adding a CALL must not rescan an unrelated ASM
     * table: doing so made a function with many CALLs and ASM sites quadratic
     * and coupled two independently owned IR collections. */
    for (index = 0U; index < function->callee_count; ++index) {
        const MinicCoreCallee *existing;

        existing = &function->callees[index];
        if (existing->name_length == name_length &&
            memcmp(existing->name, name, name_length) == 0) {
            if (!callee_signature_equal(existing, &draft->callees[index],
                                        return_type,
                                        parameter_types,
                                        parameter_count,
                                        is_variadic,
                                        calling_convention)) {
                return false;
            }
            *callee_id = (MinicCoreCalleeId)index;
            return true;
        }
    }
    (void)memset(&stored, 0, sizeof(stored));
    (void)memset(&source_stored, 0, sizeof(source_stored));
    stored.name = minic_core_ir_copy_name(name, name_length);
    if (stored.name == NULL) {
        return false;
    }
    if (parameter_count != 0U) {
        source_stored.parameter_types =
            (MinicType *)malloc(parameter_count * sizeof(*source_stored.parameter_types));
        if (source_stored.parameter_types == NULL) {
            free(stored.name);
            return false;
        }
        (void)memcpy(source_stored.parameter_types,
                     parameter_types,
                     parameter_count * sizeof(*source_stored.parameter_types));
    }
    stored.name_length = name_length;
    stored.core_function_type_id = MINIC_CORE_TYPE_INVALID;
    source_stored.function_type_id = MINIC_FUNCTION_TYPE_INVALID;
    source_stored.return_type = return_type;
    source_stored.parameter_count = parameter_count;
    source_stored.is_variadic = is_variadic;
    source_stored.calling_convention = calling_convention;
    if (!minic_core_ir_grow_array((void **)&function->callees,
                    &function->callee_capacity,
                    function->callee_count,
                    sizeof(*function->callees)) ||
        !minic_core_ir_grow_array((void **)&draft->callees,
                    &draft->callee_capacity,
                    draft->callee_count,
                    sizeof(*draft->callees))) {
        free(stored.name);
        free(source_stored.parameter_types);
        return false;
    }
    function->callees[function->callee_count] = stored;
    draft->callees[draft->callee_count] = source_stored;
    *callee_id = (MinicCoreCalleeId)function->callee_count;
    function->callee_count += 1U;
    draft->callee_count += 1U;
    return true;
}

static bool call_signature_equal(const MinicCoreSourceCallType *source,
                                 MinicFunctionTypeId function_type_id,
                                 MinicType return_type,
                                 const MinicType *parameter_types,
                                 size_t parameter_count,
                                 bool is_variadic,
                                 MinicFunctionCallingConvention calling_convention) {
    size_t index;

    if (source == NULL ||
        source->function_type_id != function_type_id ||
        !minic_type_equal(source->return_type, return_type) ||
        source->parameter_count != parameter_count ||
        source->is_variadic != is_variadic ||
        source->calling_convention != calling_convention) {
        return false;
    }
    for (index = 0U; index < parameter_count; ++index) {
        if (!minic_type_equal(source->parameter_types[index], parameter_types[index])) {
            return false;
        }
    }
    return true;
}

bool minic_core_function_add_call_signature(MinicCoreSourceBuilder *builder,
                                            MinicFunctionTypeId function_type_id,
                                            MinicType return_type,
                                            const MinicType *parameter_types,
                                            size_t parameter_count,
                                            bool is_variadic,
                                            MinicFunctionCallingConvention calling_convention,
                                            MinicCoreCallSignatureId *signature_id) {
    MinicCoreFunction *function = builder == NULL ? NULL : &builder->core;
    MinicCoreCallSignature stored;
    MinicCoreSourceCallType source_stored;
    MinicCoreSourceDraft *draft;
    size_t index;

    if (function == NULL || function->type_context != NULL ||
        (draft = builder->source_draft) == NULL ||
        draft->call_signature_count != function->call_signature_count ||
        signature_id == NULL ||
        function_type_id == MINIC_FUNCTION_TYPE_INVALID ||
        !minic_function_calling_convention_valid(calling_convention) ||
        function->call_signature_count >= (size_t)UINT32_MAX ||
        (!minic_type_is_void(return_type) && !minic_type_is_record(return_type) &&
         !minic_core_ir_call_scalar_type(return_type)) ||
        (parameter_count != 0U && parameter_types == NULL) ||
        parameter_count > SIZE_MAX / sizeof(*source_stored.parameter_types)) {
        return false;
    }
    for (index = 0U; index < parameter_count; ++index) {
        if (!minic_core_ir_call_parameter_type(parameter_types[index])) {
            return false;
        }
    }
    for (index = 0U; index < function->call_signature_count; ++index) {
        if (call_signature_equal(&draft->call_signatures[index],
                                 function_type_id,
                                 return_type,
                                 parameter_types,
                                 parameter_count,
                                 is_variadic,
                                 calling_convention)) {
            *signature_id = (MinicCoreCallSignatureId)index;
            return true;
        }
    }
    (void)memset(&stored, 0, sizeof(stored));
    (void)memset(&source_stored, 0, sizeof(source_stored));
    source_stored.function_type_id = function_type_id;
    stored.core_function_type_id = MINIC_CORE_TYPE_INVALID;
    source_stored.return_type = return_type;
    source_stored.parameter_count = parameter_count;
    source_stored.is_variadic = is_variadic;
    source_stored.calling_convention = calling_convention;
    if (parameter_count != 0U) {
        source_stored.parameter_types =
            (MinicType *)malloc(parameter_count * sizeof(*source_stored.parameter_types));
        if (source_stored.parameter_types == NULL) {
            return false;
        }
        (void)memcpy(source_stored.parameter_types,
                     parameter_types,
                     parameter_count * sizeof(*source_stored.parameter_types));
    }
    if (!minic_core_ir_grow_array((void **)&function->call_signatures,
                    &function->call_signature_capacity,
                    function->call_signature_count,
                    sizeof(*function->call_signatures)) ||
        !minic_core_ir_grow_array((void **)&draft->call_signatures,
                    &draft->call_signature_capacity,
                    draft->call_signature_count,
                    sizeof(*draft->call_signatures))) {
        free(source_stored.parameter_types);
        return false;
    }
    function->call_signatures[function->call_signature_count] = stored;
    draft->call_signatures[draft->call_signature_count] = source_stored;
    *signature_id = (MinicCoreCallSignatureId)function->call_signature_count;
    function->call_signature_count += 1U;
    draft->call_signature_count += 1U;
    return true;
}

/* C producers attach one exact source descriptor to each instruction, never
 * to a duplicate SSA Value. Only C ingress supplies unresolved CoreType
 * placeholders; independent producers append fully typed Core instructions. */
static MinicCoreInstruction minic_source_placeholder(
    const MinicCoreInstruction *input) {
    MinicCoreInstruction result = *input;
    result.core_type_id = MINIC_CORE_TYPE_INVALID;
    if (result.kind == MINIC_CORE_INSTRUCTION_FIELD_ADDRESS)
        result.value.field_address.core_record_type_id = MINIC_CORE_TYPE_INVALID;
    return result;
}

bool minic_core_function_append_source_value_instruction(
    MinicCoreSourceBuilder *builder, MinicCoreBlockId block_id,
    const MinicCoreInstruction *instruction, MinicType source_type,
    MinicCoreValueId *value_id) {
    MinicCoreFunction *function = builder == NULL ? NULL : &builder->core;
    MinicCoreSourceDraft *draft;
    MinicCoreInstruction placeholder;

    if (instruction == NULL) return false;
    placeholder = minic_source_placeholder(instruction);
    if (function == NULL || function->type_context != NULL ||
        (draft = builder->source_draft) == NULL ||
        draft->instruction_type_count != function->instruction_count ||
        !minic_core_ir_grow_array((void **)&draft->instruction_types,
                    &draft->instruction_type_capacity,
                    draft->instruction_type_count,
                    sizeof(*draft->instruction_types)) ||
        !minic_core_function_append_value_instruction(
            function, block_id, &placeholder, value_id))
        return false;
    draft->instruction_types[draft->instruction_type_count] = source_type;
    draft->instruction_type_count += 1U;
    return true;
}

bool minic_core_function_append_source_effect_instruction(
    MinicCoreSourceBuilder *builder, MinicCoreBlockId block_id,
    const MinicCoreInstruction *instruction, MinicType source_type) {
    MinicCoreFunction *function = builder == NULL ? NULL : &builder->core;
    MinicCoreSourceDraft *draft;
    MinicCoreInstruction placeholder;

    if (instruction == NULL) return false;
    placeholder = minic_source_placeholder(instruction);
    if (function == NULL || function->type_context != NULL ||
        (draft = builder->source_draft) == NULL ||
        draft->instruction_type_count != function->instruction_count ||
        !minic_core_ir_grow_array((void **)&draft->instruction_types,
                    &draft->instruction_type_capacity,
                    draft->instruction_type_count,
                    sizeof(*draft->instruction_types)) ||
        !minic_core_function_append_effect_instruction(
            function, block_id, &placeholder))
        return false;
    draft->instruction_types[draft->instruction_type_count] = source_type;
    draft->instruction_type_count += 1U;
    return true;
}

/* A failed C type import must never attach a partially typed function.
 * Cleanup is idempotent so the original source draft can be retried. */
static void clear_core_type_snapshots(MinicCoreFunction *function) {
    size_t index;

    if (function == NULL) return;
    function->type_context = NULL;
    function->core_return_type_id = MINIC_CORE_TYPE_INVALID;
    free(function->core_parameter_type_ids);
    function->core_parameter_type_ids = NULL;
    for (index = 0U; index < function->fixed_register_binding_count; ++index)
        function->fixed_register_bindings[index].core_type_id = MINIC_CORE_TYPE_INVALID;
    for (index = 0U; index < function->object_count; ++index)
        function->objects[index].core_type_id = MINIC_CORE_TYPE_INVALID;
    for (index = 0U; index < function->global_count; ++index)
        function->globals[index].core_type_id = MINIC_CORE_TYPE_INVALID;
    for (index = 0U; index < function->callee_count; ++index) {
        function->callees[index].core_function_type_id = MINIC_CORE_TYPE_INVALID;
    }
    for (index = 0U; index < function->call_signature_count; ++index) {
        MinicCoreCallSignature *signature = &function->call_signatures[index];
        signature->core_function_type_id = MINIC_CORE_TYPE_INVALID;
    }
    for (index = 0U; index < function->value_count; ++index)
        function->values[index].core_type_id = MINIC_CORE_TYPE_INVALID;
    for (index = 0U; index < function->instruction_count; ++index) {
        MinicCoreInstruction *instruction = &function->instructions[index];
        instruction->core_type_id = MINIC_CORE_TYPE_INVALID;
        if (instruction->kind == MINIC_CORE_INSTRUCTION_FIELD_ADDRESS)
            instruction->value.field_address.core_record_type_id = MINIC_CORE_TYPE_INVALID;
    }
}

static bool snapshot_one_type(const MinicCoreTypeContext *context,
                              MinicCoreTypeSnapshotResolver resolver,
                              void *user_data, MinicType source,
                              MinicCoreTypeId *result) {
    return resolver(user_data, source, result) &&
           minic_core_type_get(context, *result) != NULL;
}

static bool snapshot_type_array(const MinicCoreTypeContext *context,
                                MinicCoreTypeSnapshotResolver resolver,
                                void *user_data, const MinicType *source,
                                size_t count, MinicCoreTypeId **result) {
    size_t index;
    MinicCoreTypeId *copy;

    if (result == NULL || (count != 0U && source == NULL) ||
        count > SIZE_MAX / sizeof(*copy)) return false;
    *result = NULL;
    if (count == 0U) return true;
    copy = malloc(count * sizeof(*copy));
    if (copy == NULL) return false;
    *result = copy;
    for (index = 0U; index < count; ++index)
        if (!snapshot_one_type(context, resolver, user_data, source[index], &copy[index]))
            return false;
    return true;
}

static bool ingress_storage_valid(const void *items, size_t count, size_t capacity) {
    return count <= capacity && (capacity == 0U || items != NULL);
}

MinicType minic_core_function_source_value_type(const MinicCoreSourceBuilder *builder,
                                                 MinicCoreValueId value_id) {
    const MinicType invalid = {.base_kind = (MinicTypeBaseKind)0x7fffffff};
    const MinicCoreFunction *function = builder == NULL ? NULL : &builder->core;
    MinicCoreInstructionId definition;
    const MinicCoreSourceDraft *draft;
    if (function == NULL || function->type_context != NULL ||
        builder->source_draft == NULL ||
        function->values == NULL || function->instructions == NULL ||
        value_id >= function->value_count)
        return invalid;
    draft = builder->source_draft;
    definition = function->values[value_id].definition;
    if (draft->instruction_types == NULL ||
        definition >= function->instruction_count ||
        definition >= draft->instruction_type_count ||
        function->instructions[definition].result != value_id)
        return invalid;
    return draft->instruction_types[definition];
}

static bool ingress_enum_scalar_registered(const MinicCoreSourceBuilder *builder,
                                           MinicType type) {
    MinicType effective;
    return !minic_type_is_enum(type) ||
           minic_core_function_effective_integer_type(builder, type, &effective);
}

bool minic_core_function_verify_source_ingress(const MinicCoreSourceBuilder *builder) {
    const MinicCoreFunction *function = builder == NULL ? NULL : &builder->core;
    const MinicCoreSourceDraft *draft;
    size_t i, j;

    if (function == NULL || function->type_context != NULL ||
        builder->source_draft == NULL)
        return false;
    draft = builder->source_draft;
    if (
        function->name == NULL || function->name_length == 0U ||
        function->name_length == SIZE_MAX ||
        (function->parameter_count != 0U && draft->parameter_types == NULL) ||
        function->parameter_count > SIZE_MAX / sizeof(*draft->parameter_types) ||
        !ingress_storage_valid(function->fixed_register_bindings,
                               function->fixed_register_binding_count,
                               function->fixed_register_binding_capacity) ||
        !ingress_storage_valid(draft->enum_types, draft->enum_type_count,
                               draft->enum_type_capacity) ||
        !ingress_storage_valid(draft->fixed_register_binding_types,
                               draft->fixed_register_binding_type_count,
                               draft->fixed_register_binding_type_capacity) ||
        draft->fixed_register_binding_type_count != function->fixed_register_binding_count ||
        !ingress_storage_valid(draft->object_types,
                               draft->object_type_count, draft->object_type_capacity) ||
        draft->object_type_count != function->object_count ||
        !ingress_storage_valid(draft->global_types,
                               draft->global_type_count, draft->global_type_capacity) ||
        draft->global_type_count != function->global_count ||
        !ingress_storage_valid(draft->callees,
                               draft->callee_count, draft->callee_capacity) ||
        draft->callee_count != function->callee_count ||
        !ingress_storage_valid(draft->call_signatures,
                               draft->call_signature_count, draft->call_signature_capacity) ||
        draft->call_signature_count != function->call_signature_count ||
        !ingress_storage_valid(draft->instruction_types,
                               draft->instruction_type_count, draft->instruction_type_capacity) ||
        draft->instruction_type_count != function->instruction_count ||
        !ingress_storage_valid(function->objects, function->object_count,
                               function->object_capacity) ||
        !ingress_storage_valid(function->globals, function->global_count,
                               function->global_capacity) ||
        !ingress_storage_valid(function->function_symbols,
                               function->function_symbol_count,
                               function->function_symbol_capacity) ||
        !ingress_storage_valid(function->callees, function->callee_count,
                               function->callee_capacity) ||
        !ingress_storage_valid(function->call_signatures,
                               function->call_signature_count,
                               function->call_signature_capacity) ||
        !ingress_storage_valid(function->inline_asms, function->inline_asm_count,
                               function->inline_asm_capacity) ||
        !ingress_storage_valid(function->call_arguments,
                               function->call_argument_count,
                               function->call_argument_capacity) ||
        !ingress_storage_valid(function->values, function->value_count,
                               function->value_capacity) ||
        !ingress_storage_valid(function->instructions,
                               function->instruction_count,
                               function->instruction_capacity) ||
        !ingress_storage_valid(function->blocks, function->block_count,
                               function->block_capacity) ||
        function->block_count == 0U || function->entry_block != 0U ||
        function->value_count >= (size_t)UINT32_MAX ||
        function->instruction_count >= (size_t)UINT32_MAX ||
        function->block_count >= (size_t)UINT32_MAX ||
        function->value_count > function->instruction_count ||
        !minic_function_calling_convention_valid(function->calling_convention) ||
        !ingress_enum_scalar_registered(builder, draft->return_type))
        return false;
    for (i = 0U; i < function->parameter_count; ++i)
        if (!ingress_enum_scalar_registered(builder, draft->parameter_types[i]))
            return false;
    for (i = 0U; i < draft->enum_type_count; ++i)
        if (draft->enum_types[i].enum_id == MINIC_ENUM_INVALID ||
            !minic_type_is_integer(draft->enum_types[i].effective_integer_type) ||
            minic_type_is_enum(draft->enum_types[i].effective_integer_type))
            return false;
    for (i = 0U; i < function->fixed_register_binding_count; ++i) {
        const MinicCoreFixedRegisterBinding *binding =
            &function->fixed_register_bindings[i];
        const MinicType source_type = draft->fixed_register_binding_types[i];
        if (binding->register_name == NULL || binding->register_name_length == 0U ||
            (!minic_type_is_integer(source_type) &&
             !minic_type_is_pointer(source_type) &&
             !minic_type_is_double(source_type)) ||
            !ingress_enum_scalar_registered(builder, source_type))
            return false;
    }
    for (i = 0U; i < function->function_symbol_count; ++i)
        if (function->function_symbols[i].name == NULL ||
            function->function_symbols[i].name_length == 0U) return false;
    for (i = 0U; i < function->object_count; ++i)
        if (minic_type_is_void(draft->object_types[i]) ||
            minic_type_is_function(draft->object_types[i]) ||
            !ingress_enum_scalar_registered(builder, draft->object_types[i]))
            return false;
    for (i = 0U; i < function->global_count; ++i)
        if (function->globals[i].name == NULL ||
            function->globals[i].name_length == 0U ||
            !minic_core_ir_global_addressable_type(draft->global_types[i]) ||
            !ingress_enum_scalar_registered(builder, draft->global_types[i]))
            return false;
    for (i = 0U; i < function->callee_count; ++i)
        if (function->callees[i].name == NULL ||
            draft->callees[i].function_type_id != MINIC_FUNCTION_TYPE_INVALID ||
            (draft->callees[i].parameter_count != 0U &&
             draft->callees[i].parameter_types == NULL) ||
            draft->callees[i].parameter_count >
                SIZE_MAX / sizeof(*draft->callees[i].parameter_types) ||
            !minic_function_calling_convention_valid(draft->callees[i].calling_convention) ||
            !minic_core_ir_direct_call_return_type(draft->callees[i].return_type) ||
            !ingress_enum_scalar_registered(builder, draft->callees[i].return_type))
            return false;
    for (i = 0U; i < function->callee_count; ++i)
        for (j = 0U; j < draft->callees[i].parameter_count; ++j)
            if (!minic_core_ir_call_parameter_type(draft->callees[i].parameter_types[j]) ||
                !ingress_enum_scalar_registered(builder, draft->callees[i].parameter_types[j]))
                return false;
    for (i = 0U; i < function->call_signature_count; ++i)
        if ((draft->call_signatures[i].parameter_count != 0U &&
             draft->call_signatures[i].parameter_types == NULL) ||
            draft->call_signatures[i].parameter_count >
                SIZE_MAX / sizeof(*draft->call_signatures[i].parameter_types) ||
            draft->call_signatures[i].function_type_id == MINIC_FUNCTION_TYPE_INVALID ||
            !minic_function_calling_convention_valid(
                draft->call_signatures[i].calling_convention) ||
            !minic_core_ir_direct_call_return_type(draft->call_signatures[i].return_type) ||
            !ingress_enum_scalar_registered(builder, draft->call_signatures[i].return_type))
            return false;
    for (i = 0U; i < function->call_signature_count; ++i)
        for (j = 0U; j < draft->call_signatures[i].parameter_count; ++j)
            if (!minic_core_ir_call_parameter_type(
                    draft->call_signatures[i].parameter_types[j]) ||
                !ingress_enum_scalar_registered(
                    builder, draft->call_signatures[i].parameter_types[j]))
                return false;
    for (i = 0U; i < function->value_count; ++i)
        if (function->values[i].definition >= function->instruction_count ||
            !ingress_enum_scalar_registered(
                builder, minic_core_function_source_value_type(builder, (MinicCoreValueId)i)))
            return false;
    for (i = 0U; i < function->instruction_count; ++i) {
        const MinicCoreInstruction *instruction = &function->instructions[i];
        if ((unsigned int)instruction->kind >=
                MINIC_CORE_INSTRUCTION_KIND_COUNT ||
            (instruction->result != MINIC_CORE_VALUE_INVALID &&
             instruction->result >= function->value_count) ||
            !ingress_enum_scalar_registered(builder, draft->instruction_types[i]))
            return false;
        if (instruction->kind == MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW) {
            MinicType pointee;
            const MinicCoreValueId address = instruction->value.integer_overflow.result_address;
            /* Checked-overflow destination eligibility belongs exclusively
             * to C source admission. Core's execution integer type cannot
             * tell a plain integer apart from a source enum with the same
             * representation, and must never carry a C-origin side flag. */
            if (address >= function->value_count ||
                !minic_type_pointee(minic_core_function_source_value_type(builder, address),
                                    &pointee) ||
                !minic_type_is_integer(pointee) || minic_type_is_bool_integer(pointee) ||
                minic_type_is_enum(pointee) || minic_type_is_const(pointee) ||
                minic_type_is_volatile(pointee))
                return false;
        }
    }
    for (i = 0U; i < function->block_count; ++i) {
        const MinicCoreBlock *block = &function->blocks[i];
        if (!ingress_storage_valid(block->instructions, block->instruction_count,
                                   block->instruction_capacity) ||
            !block->has_terminator) return false;
        for (j = 0U; j < block->instruction_count; ++j)
            if (block->instructions[j] >= function->instruction_count) return false;
    }
    return true;
}

bool minic_core_function_snapshot_types(MinicCoreSourceBuilder *builder,
                                        MinicCoreTypeContext *context,
                                        MinicCoreTypeSnapshotResolver resolver,
                                        void *user_data) {
    MinicCoreFunction *function = builder == NULL ? NULL : &builder->core;
    const MinicCoreSourceDraft *draft;
    size_t index;

    if (function == NULL || context == NULL || resolver == NULL ||
        function->type_context != NULL ||
        !minic_core_function_verify_source_ingress(builder))
        return false;
    draft = builder->source_draft;
    /* This cleanup is deliberately idempotent. It also permits a caller to
     * retry with a fresh importer after an earlier failed translation. */
    clear_core_type_snapshots(function);
    if (!snapshot_one_type(context, resolver, user_data, draft->return_type,
                           &function->core_return_type_id) ||
        !snapshot_type_array(context, resolver, user_data,
                             draft->parameter_types, function->parameter_count,
                             &function->core_parameter_type_ids)) goto failed;
    for (index = 0U; index < function->fixed_register_binding_count; ++index)
        if (!snapshot_one_type(context, resolver, user_data,
                               draft->fixed_register_binding_types[index],
                               &function->fixed_register_bindings[index].core_type_id))
            goto failed;
    for (index = 0U; index < function->object_count; ++index)
        if (!snapshot_one_type(context, resolver, user_data,
                               draft->object_types[index],
                               &function->objects[index].core_type_id))
            goto failed;
    for (index = 0U; index < function->global_count; ++index)
        if (!snapshot_one_type(context, resolver, user_data,
                               draft->global_types[index],
                               &function->globals[index].core_type_id))
            goto failed;
    for (index = 0U; index < function->callee_count; ++index) {
        MinicCoreCallee *callee = &function->callees[index];
        const MinicCoreSourceCallType *source_callee = &draft->callees[index];
        MinicCoreType direct_signature = {0};
        MinicCoreTypeId *parameters = NULL;
        direct_signature.kind = MINIC_CORE_TYPE_FUNCTION;
        direct_signature.value.function.parameter_count = source_callee->parameter_count;
        direct_signature.value.function.is_variadic = source_callee->is_variadic;
        direct_signature.value.function.calling_convention = source_callee->calling_convention;
        if (!snapshot_one_type(context, resolver, user_data, source_callee->return_type,
                               &direct_signature.value.function.return_type) ||
            !snapshot_type_array(context, resolver, user_data,
                                 source_callee->parameter_types,
                                 source_callee->parameter_count, &parameters)) {
            free(parameters);
            goto failed;
        }
        direct_signature.value.function.parameters = parameters;
        if (!minic_core_type_register(context, &direct_signature,
                                      &callee->core_function_type_id)) {
            free(parameters);
            goto failed;
        }
        free(parameters);
    }
    for (index = 0U; index < function->call_signature_count; ++index) {
        MinicCoreCallSignature *signature = &function->call_signatures[index];
        const MinicCoreSourceCallType *source_signature = &draft->call_signatures[index];
        const MinicCoreType *imported_function;
        MinicCoreType function_snapshot;
        MinicCoreTypeId imported_return;
        size_t parameter_index;
        /* C's declaration-table ID lives only in this migration adapter.
         * The resulting Core FUNCTION ID is the sole executable contract.
         * Compare the source draft exactly once at the ingress boundary,
         * without retaining a second Core parameter/ABI snapshot. */
        const MinicType source_function = minic_type_function(source_signature->function_type_id);
        if (!snapshot_one_type(context, resolver, user_data, source_function,
                               &signature->core_function_type_id)) goto failed;
        imported_function = minic_core_type_get(
            context, minic_core_type_unqualified(context,
                signature->core_function_type_id));
        if (imported_function == NULL) goto failed;
        function_snapshot = *imported_function;
        if (function_snapshot.kind != MINIC_CORE_TYPE_FUNCTION ||
            function_snapshot.value.function.parameter_count !=
                source_signature->parameter_count ||
            function_snapshot.value.function.is_variadic !=
                source_signature->is_variadic ||
            function_snapshot.value.function.calling_convention !=
                source_signature->calling_convention ||
            (source_signature->parameter_count != 0U &&
             function_snapshot.value.function.parameters == NULL) ||
            !snapshot_one_type(context, resolver, user_data,
                source_signature->return_type, &imported_return) ||
            function_snapshot.value.function.return_type != imported_return)
            goto failed;
        for (parameter_index = 0U;
             parameter_index < source_signature->parameter_count; ++parameter_index) {
            MinicCoreTypeId imported_parameter;
            if (!snapshot_one_type(context, resolver, user_data,
                    source_signature->parameter_types[parameter_index],
                    &imported_parameter) ||
                function_snapshot.value.function.parameters[parameter_index] !=
                    imported_parameter)
                goto failed;
        }
    }
    for (index = 0U; index < function->value_count; ++index)
        if (!snapshot_one_type(context, resolver, user_data,
                               minic_core_function_source_value_type(builder, (MinicCoreValueId)index),
                               &function->values[index].core_type_id)) goto failed;
    for (index = 0U; index < function->instruction_count; ++index) {
        MinicCoreInstruction *instruction = &function->instructions[index];
        if (!snapshot_one_type(context, resolver, user_data, draft->instruction_types[index],
                               &instruction->core_type_id)) goto failed;
        if (instruction->kind == MINIC_CORE_INSTRUCTION_FIELD_ADDRESS) {
            const MinicCoreType *pointer, *record;
            const MinicCoreValueId base = instruction->value.field_address.base;
            MinicCoreTypeId pointer_id, record_id;
            if (base >= function->value_count) goto failed;
            pointer_id = minic_core_type_unqualified(
                context, function->values[base].core_type_id);
            pointer = minic_core_type_get(context, pointer_id);
            if (pointer == NULL || pointer->kind != MINIC_CORE_TYPE_POINTER)
                goto failed;
            record_id = minic_core_type_unqualified(context, pointer->value.pointer.pointee);
            record = minic_core_type_get(context, record_id);
            if (record == NULL || record->kind != MINIC_CORE_TYPE_RECORD)
                goto failed;
            /* The lowerer already checked the source nominal record ID.
             * After import the base pointer owns that identity; no C
             * RecordId needs to cross the Core boundary. */
            instruction->value.field_address.core_record_type_id = record_id;
        }
    }
    if (minic_core_function_attach_type_context(function, context)) {
        minic_core_source_draft_release(builder->source_draft);
        builder->source_draft = NULL;
        return true;
    }
failed:
    clear_core_type_snapshots(function);
    return false;
}
