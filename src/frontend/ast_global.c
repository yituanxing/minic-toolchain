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

bool minic_c0_program_add_global_object(MinicC0Program *program,
                                        const char *name,
                                        size_t name_length,
                                        MinicType type,
                                        bool is_internal,
                                        bool is_read_only,
                                        MinicGlobalObjectId *global_object_id) {
    MinicGlobalObject object;

    if (program == NULL || name == NULL || global_object_id == NULL || minic_type_is_void(type) ||
        name_conflicts(program, name, name_length)) {
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
    *global_object_id = program->global_object_count;
    program->global_objects[program->global_object_count] = object;
    program->global_object_count += 1U;
    return true;
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
