#include "frontend/ast.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static bool grow_array(void **data, size_t *capacity, size_t count, size_t element_size) {
    void *resized;
    size_t new_capacity;

    if (count < *capacity) {
        return true;
    }
    new_capacity = *capacity == 0U ? 16U : *capacity * 2U;
    if (new_capacity < *capacity || new_capacity > SIZE_MAX / element_size) {
        return false;
    }
    resized = realloc(*data, new_capacity * element_size);
    if (resized == NULL) {
        return false;
    }
    *data = resized;
    *capacity = new_capacity;
    return true;
}

static char *copy_name(const char *name, size_t name_length) {
    char *copy;

    if (name == NULL || name_length == SIZE_MAX) {
        return NULL;
    }
    copy = (char *)malloc(name_length + 1U);
    if (copy == NULL) {
        return NULL;
    }
    (void)memcpy(copy, name, name_length);
    copy[name_length] = '\0';
    return copy;
}

static bool name_conflicts(const MinicC0Program *program, const char *name, size_t name_length) {
    size_t index;

    for (index = 0U; index < program->fixed_register_binding_count; ++index) {
        const MinicFixedRegisterBinding *binding;

        binding = &program->fixed_register_bindings[index];
        if (binding->name_length == name_length && memcmp(binding->name, name, name_length) == 0) {
            return true;
        }
    }
    for (index = 0U; index < program->global_object_count; ++index) {
        const MinicGlobalObject *object;

        object = &program->global_objects[index];
        if (object->name_length == name_length && memcmp(object->name, name, name_length) == 0) {
            return true;
        }
    }
    for (index = 0U; index < program->function_count; ++index) {
        const MinicFunction *function;

        function = &program->functions[index];
        if (function->name_length == name_length &&
            memcmp(function->name, name, name_length) == 0) {
            return true;
        }
    }
    return false;
}

bool minic_c0_program_add_fixed_register_binding(MinicC0Program *program,
                                                 const char *name,
                                                 size_t name_length,
                                                 MinicType type,
                                                 const char *register_name,
                                                 size_t register_name_length,
                                                 MinicFixedRegisterBindingId *binding_id) {
    MinicFixedRegisterBinding binding;

    if (program == NULL || name == NULL || register_name == NULL || binding_id == NULL ||
        register_name_length == 0U ||
        (!minic_type_is_integer(type) && !minic_type_is_pointer(type)) ||
        name_conflicts(program, name, name_length) ||
        !grow_array((void **)&program->fixed_register_bindings,
                    &program->fixed_register_binding_capacity,
                    program->fixed_register_binding_count,
                    sizeof(*program->fixed_register_bindings))) {
        return false;
    }
    (void)memset(&binding, 0, sizeof(binding));
    binding.name = copy_name(name, name_length);
    binding.register_name = copy_name(register_name, register_name_length);
    if (binding.name == NULL || binding.register_name == NULL) {
        free(binding.name);
        free(binding.register_name);
        return false;
    }
    binding.name_length = name_length;
    binding.register_name_length = register_name_length;
    binding.type = type;
    *binding_id = program->fixed_register_binding_count;
    program->fixed_register_bindings[program->fixed_register_binding_count] = binding;
    program->fixed_register_binding_count += 1U;
    return true;
}

static bool add_global_object_entity(MinicC0Program *program,
                                     const char *name,
                                     size_t name_length,
                                     MinicType type,
                                     bool is_internal,
                                     bool is_read_only,
                                     bool is_extern,
                                     MinicGlobalObjectId *global_object_id) {
    MinicGlobalObject object;

    if (program == NULL || name == NULL || global_object_id == NULL ||
        (minic_type_is_void(type) && !is_extern) || name_conflicts(program, name, name_length)) {
        return false;
    }
    if (!grow_array((void **)&program->global_objects,
                    &program->global_object_capacity,
                    program->global_object_count,
                    sizeof(*program->global_objects))) {
        return false;
    }

    (void)memset(&object, 0, sizeof(object));
    object.name = copy_name(name, name_length);
    if (object.name == NULL) {
        return false;
    }
    object.name_length = name_length;
    object.type = type;
    object.is_internal = is_internal;
    object.is_read_only = is_read_only;
    object.is_extern = is_extern;
    *global_object_id = program->global_object_count;
    program->global_objects[program->global_object_count] = object;
    program->global_object_count += 1U;
    return true;
}

bool minic_c0_program_add_global_object(MinicC0Program *program,
                                        const char *name,
                                        size_t name_length,
                                        MinicType type,
                                        bool is_internal,
                                        bool is_read_only,
                                        MinicGlobalObjectId *global_object_id) {
    return add_global_object_entity(
        program, name, name_length, type, is_internal, is_read_only, false, global_object_id);
}

bool minic_c0_program_add_extern_global_object(MinicC0Program *program,
                                               const char *name,
                                               size_t name_length,
                                               MinicType type,
                                               bool is_read_only,
                                               MinicGlobalObjectId *global_object_id) {
    return add_global_object_entity(
        program, name, name_length, type, false, is_read_only, true, global_object_id);
}

bool minic_c0_global_object_add_initializer(MinicC0Program *program,
                                            MinicGlobalObjectId global_object_id,
                                            int value) {
    MinicGlobalObject *object;

    if (program == NULL || global_object_id >= program->global_object_count) {
        return false;
    }
    object = &program->global_objects[global_object_id];
    if (object->is_zero_initialized || object->function_relocation_count != 0U) {
        return false;
    }
    if (!grow_array((void **)&object->initializer_values,
                    &object->initializer_capacity,
                    object->initializer_count,
                    sizeof(*object->initializer_values))) {
        return false;
    }
    object->initializer_values[object->initializer_count] = value;
    object->initializer_count += 1U;
    return true;
}

bool minic_c0_global_object_add_function_relocation(MinicC0Program *program,
                                                    MinicGlobalObjectId global_object_id,
                                                    size_t field_index,
                                                    MinicFunctionId function_id) {
    MinicGlobalObject *object;
    MinicGlobalFunctionRelocation *relocation;

    if (program == NULL || global_object_id >= program->global_object_count ||
        function_id >= program->function_count) {
        return false;
    }
    object = &program->global_objects[global_object_id];
    if (object->initializer_count != 0U || object->function_relocation_count >= 8U) {
        return false;
    }
    relocation = &object->function_relocations[object->function_relocation_count];
    relocation->field_index = field_index;
    relocation->function_id = function_id;
    object->function_relocation_count += 1U;
    return true;
}

bool minic_c0_global_object_set_extern(MinicC0Program *program,
                                       MinicGlobalObjectId global_object_id) {
    MinicGlobalObject *object;

    if (program == NULL || global_object_id >= program->global_object_count) {
        return false;
    }
    object = &program->global_objects[global_object_id];
    if (object->initializer_count != 0U || object->function_relocation_count != 0U ||
        object->object_relocation_count != 0U || object->is_zero_initialized ||
        object->is_internal) {
        return false;
    }
    object->is_extern = true;
    return true;
}

bool minic_c0_global_object_add_object_relocation(MinicC0Program *program,
                                                  MinicGlobalObjectId global_object_id,
                                                  size_t element_index,
                                                  MinicGlobalObjectId target_object_id) {
    MinicGlobalObject *object;
    MinicGlobalObjectRelocation *relocation;

    if (program == NULL || global_object_id >= program->global_object_count ||
        target_object_id >= program->global_object_count || global_object_id == target_object_id) {
        return false;
    }
    object = &program->global_objects[global_object_id];
    if (object->initializer_count != 0U || object->function_relocation_count != 0U ||
        !grow_array((void **)&object->object_relocations,
                    &object->object_relocation_capacity,
                    object->object_relocation_count,
                    sizeof(*object->object_relocations))) {
        return false;
    }
    relocation = &object->object_relocations[object->object_relocation_count];
    relocation->element_index = element_index;
    relocation->target_object_id = target_object_id;
    object->object_relocation_count += 1U;
    return true;
}

bool minic_c0_global_object_set_zero_initialized(MinicC0Program *program,
                                                 MinicGlobalObjectId global_object_id) {
    MinicGlobalObject *object;

    if (program == NULL || global_object_id >= program->global_object_count) {
        return false;
    }
    object = &program->global_objects[global_object_id];
    if (object->initializer_count != 0U) {
        return false;
    }
    object->is_zero_initialized = true;
    return true;
}

const MinicGlobalObject *minic_c0_program_global_object(const MinicC0Program *program,
                                                        MinicGlobalObjectId global_object_id) {
    if (program == NULL || global_object_id >= program->global_object_count) {
        return NULL;
    }
    return &program->global_objects[global_object_id];
}

const MinicFixedRegisterBinding *
minic_c0_program_fixed_register_binding(const MinicC0Program *program,
                                        MinicFixedRegisterBindingId binding_id) {
    if (program == NULL || binding_id >= program->fixed_register_binding_count) {
        return NULL;
    }
    return &program->fixed_register_bindings[binding_id];
}

bool minic_c0_global_object_set_visibility(MinicC0Program *program,
                                           MinicGlobalObjectId global_object_id,
                                           MinicSymbolVisibility visibility) {
    MinicGlobalObject *object;

    if (program == NULL || global_object_id >= program->global_object_count ||
        visibility < MINIC_SYMBOL_VISIBILITY_DEFAULT ||
        visibility > MINIC_SYMBOL_VISIBILITY_PROTECTED) {
        return false;
    }
    object = &program->global_objects[global_object_id];
    if (object->visibility != MINIC_SYMBOL_VISIBILITY_DEFAULT && object->visibility != visibility) {
        return false;
    }
    object->visibility = visibility;
    return true;
}

bool minic_c0_global_object_set_explicit_alignment(MinicC0Program *program,
                                                   MinicGlobalObjectId global_object_id,
                                                   size_t alignment) {
    MinicGlobalObject *object;

    if (program == NULL || global_object_id >= program->global_object_count || alignment == 0U ||
        (alignment & (alignment - 1U)) != 0U) {
        return false;
    }
    object = &program->global_objects[global_object_id];
    if (alignment > object->explicit_alignment) {
        object->explicit_alignment = alignment;
    }
    return true;
}

bool minic_c0_global_object_set_section(MinicC0Program *program,
                                        MinicGlobalObjectId global_object_id,
                                        const char *name,
                                        size_t name_length) {
    MinicGlobalObject *object;
    char *copy;

    if (program == NULL || global_object_id >= program->global_object_count || name == NULL ||
        name_length == 0U || name_length == SIZE_MAX) {
        return false;
    }
    object = &program->global_objects[global_object_id];
    if (object->section_name != NULL) {
        return object->section_name_length == name_length &&
               memcmp(object->section_name, name, name_length) == 0;
    }
    copy = (char *)malloc(name_length + 1U);
    if (copy == NULL) {
        return false;
    }
    (void)memcpy(copy, name, name_length);
    copy[name_length] = '\0';
    object->section_name = copy;
    object->section_name_length = name_length;
    return true;
}
