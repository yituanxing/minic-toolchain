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

bool minic_c0_program_add_tentative_global_object(MinicC0Program *program,
                                                  const char *name,
                                                  size_t name_length,
                                                  MinicType type,
                                                  bool is_internal,
                                                  bool is_read_only,
                                                  MinicGlobalObjectId *global_object_id) {
    if (!add_global_object_entity(
            program, name, name_length, type, is_internal, is_read_only, false, global_object_id)) {
        return false;
    }
    program->global_objects[*global_object_id].is_tentative = true;
    return true;
}

static bool global_object_has_definition_payload(const MinicGlobalObject *object) {
    return object != NULL && (object->initializer_count != 0U || object->relocation_count != 0U ||
                              object->is_zero_initialized);
}

bool minic_c0_global_object_merge_tentative(MinicC0Program *program,
                                            MinicGlobalObjectId global_object_id) {
    MinicGlobalObject *object;

    if (program == NULL || global_object_id >= program->global_object_count) {
        return false;
    }
    object = &program->global_objects[global_object_id];
    if (!object->is_extern && !object->is_tentative) {
        /* A tentative definition after a full definition is another declaration
         * of the same already-defined entity. */
        return true;
    }
    if (global_object_has_definition_payload(object)) {
        return false;
    }
    object->is_extern = false;
    object->is_tentative = true;
    return true;
}

bool minic_c0_global_object_begin_definition(MinicC0Program *program,
                                             MinicGlobalObjectId global_object_id) {
    MinicGlobalObject *object;

    if (program == NULL || global_object_id >= program->global_object_count) {
        return false;
    }
    object = &program->global_objects[global_object_id];
    if ((!object->is_extern && !object->is_tentative) ||
        global_object_has_definition_payload(object)) {
        return false;
    }
    object->is_extern = false;
    object->is_tentative = false;
    object->is_block_scope_extern_only = false;
    return true;
}

bool minic_c0_global_object_add_initializer_bits(MinicC0Program *program,
                                                 MinicGlobalObjectId global_object_id,
                                                 uint64_t bits) {
    MinicGlobalObject *object;

    if (program == NULL || global_object_id >= program->global_object_count) {
        return false;
    }
    object = &program->global_objects[global_object_id];
    if (object->is_tentative || object->is_zero_initialized) {
        return false;
    }
    if (object->relocation_count != 0U) {
        size_t relocation_index;

        if (!minic_type_is_record(object->type) && !minic_type_is_array(object->type)) {
            return false;
        }
        for (relocation_index = 0U; relocation_index < object->relocation_count;
             ++relocation_index) {
            const MinicGlobalRelocation *relocation;

            relocation = &object->relocations[relocation_index];
            if ((relocation->location_kind != MINIC_GLOBAL_RELOCATION_LOCATION_RECORD_FIELD &&
                 relocation->location_kind != MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR) ||
                (relocation->location_index == object->initializer_count && bits != 0U)) {
                return false;
            }
        }
    }
    if (!grow_array((void **)&object->initializer_values,
                    &object->initializer_capacity,
                    object->initializer_count,
                    sizeof(*object->initializer_values))) {
        return false;
    }
    object->initializer_values[object->initializer_count] = bits;
    object->initializer_count += 1U;
    return true;
}

bool minic_c0_global_object_add_initializer(MinicC0Program *program,
                                            MinicGlobalObjectId global_object_id,
                                            int value) {
    return minic_c0_global_object_add_initializer_bits(
        program, global_object_id, (uint64_t)(int64_t)value);
}

static bool aggregate_scalar_slot_type(const MinicC0Program *program,
                                       MinicType type,
                                       size_t *slot_index,
                                       MinicType *slot_type) {
    if (program == NULL || slot_index == NULL || slot_type == NULL) {
        return false;
    }
    if (minic_type_is_integer(type) || minic_type_is_pointer(type)) {
        if (*slot_index == 0U) {
            *slot_type = type;
            return true;
        }
        *slot_index -= 1U;
        return false;
    }
    if (minic_type_is_array(type)) {
        const MinicArrayType *array_type;
        size_t element_index;

        array_type = minic_c0_program_array_type(program, type.array_type_id);
        if (array_type == NULL || array_type->element_count == 0U) {
            return false;
        }
        for (element_index = 0U; element_index < array_type->element_count; ++element_index) {
            size_t before = *slot_index;
            if (aggregate_scalar_slot_type(
                    program, array_type->element_type, slot_index, slot_type)) {
                return true;
            }
            if (*slot_index == before) {
                return false;
            }
        }
        return false;
    }
    if (minic_type_is_record(type)) {
        const MinicRecord *record;
        size_t field_index;
        size_t field_limit;

        record = minic_c0_program_record(program, type.record_id);
        if (record == NULL || !record->is_complete || record->field_count == 0U) {
            return false;
        }
        field_limit = record->is_union ? 1U : record->field_count;
        for (field_index = 0U; field_index < field_limit; ++field_index) {
            const MinicRecordField *field;
            size_t element_index;

            field = minic_c0_record_field(record, field_index);
            if (field == NULL || field->element_count == 0U || field->is_flexible_array) {
                return false;
            }
            for (element_index = 0U; element_index < field->element_count; ++element_index) {
                size_t before = *slot_index;
                if (aggregate_scalar_slot_type(program, field->type, slot_index, slot_type)) {
                    return true;
                }
                if (*slot_index == before) {
                    return false;
                }
            }
        }
    }
    return false;
}

static bool global_object_member_path_type(const MinicC0Program *program,
                                           const MinicGlobalObject *object,
                                           const size_t *member_indices,
                                           size_t member_depth,
                                           MinicType *result_type) {
    MinicType type;
    size_t depth;

    if (program == NULL || object == NULL || result_type == NULL ||
        member_depth > MINIC_GLOBAL_RELOCATION_MAX_MEMBER_DEPTH ||
        (member_depth != 0U && member_indices == NULL)) {
        return false;
    }
    type = object->type;
    for (depth = 0U; depth < member_depth; ++depth) {
        const MinicRecord *record;
        const MinicRecordField *field;

        if (!minic_type_is_record(type)) {
            return false;
        }
        record = minic_c0_program_record(program, type.record_id);
        field = record == NULL ? NULL : minic_c0_record_field(record, member_indices[depth]);
        if (field == NULL || field->element_count != 1U || field->is_bit_field ||
            field->is_flexible_array) {
            return false;
        }
        type = field->type;
    }
    *result_type = type;
    return true;
}

bool minic_c0_global_relocation_object_target_type(const MinicC0Program *program,
                                                   const MinicGlobalRelocation *relocation,
                                                   MinicType *target_type) {
    if (program == NULL || relocation == NULL || target_type == NULL ||
        relocation->target_kind != MINIC_GLOBAL_RELOCATION_OBJECT ||
        relocation->target_id >= program->global_object_count) {
        return false;
    }
    return global_object_member_path_type(program,
                                          &program->global_objects[relocation->target_id],
                                          relocation->target_member_indices,
                                          relocation->target_member_depth,
                                          target_type);
}

static bool global_relocation_object_target_type_compatible(const MinicC0Program *program,
                                                            MinicType slot_type,
                                                            MinicType target_type,
                                                            bool has_explicit_pointer_cast) {
    MinicType source_pointer_type;

    if (program == NULL || !minic_type_is_pointer(slot_type)) {
        return false;
    }
    if (has_explicit_pointer_cast) {
        /* The parser has already validated the explicit pointer-to-pointer cast.
         * Re-check only the normalized type-level legality here; target identity
         * and member-path validity are still verified independently. */
        if (minic_type_pointer_to(target_type, &source_pointer_type) &&
            minic_type_cast_compatible(slot_type, source_pointer_type)) {
            return true;
        }
        if (minic_type_is_array(target_type)) {
            const MinicArrayType *array_type;

            array_type = minic_c0_program_array_type(program, target_type.array_type_id);
            if (array_type != NULL &&
                minic_type_pointer_to(array_type->element_type, &source_pointer_type) &&
                minic_type_cast_compatible(slot_type, source_pointer_type)) {
                return true;
            }
        }
    }
    /* A symbolic object address can denote the object itself (`&object`). */
    if (minic_type_pointer_to(target_type, &source_pointer_type) &&
        minic_type_assignment_compatible(slot_type, source_pointer_type)) {
        return true;
    }
    /* Array-to-pointer decay and `&array[0]` have the same symbol/addend as
     * `&array`, but their C type is pointer-to-element rather than pointer-to-array.
     * Preserve that semantic alternative in the persisted relocation contract. */
    if (minic_type_is_array(target_type)) {
        const MinicArrayType *array_type;

        array_type = minic_c0_program_array_type(program, target_type.array_type_id);
        if (array_type != NULL &&
            minic_type_pointer_to(array_type->element_type, &source_pointer_type) &&
            minic_type_assignment_compatible(slot_type, source_pointer_type)) {
            return true;
        }
    }
    return false;
}

bool minic_c0_global_relocation_object_target_compatible(const MinicC0Program *program,
                                                         const MinicGlobalRelocation *relocation,
                                                         MinicType slot_type) {
    MinicType target_type;

    return minic_c0_global_relocation_object_target_type(program, relocation, &target_type) &&
           global_relocation_object_target_type_compatible(
               program, slot_type, target_type, relocation->has_explicit_pointer_cast);
}

bool minic_c0_global_relocation_slot_type(const MinicC0Program *program,
                                          const MinicGlobalObject *object,
                                          MinicGlobalRelocationLocationKind location_kind,
                                          size_t location_index,
                                          MinicType *slot_type) {
    if (program == NULL || object == NULL || slot_type == NULL) {
        return false;
    }
    if (location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_SCALAR) {
        if (location_index != 0U || !minic_type_is_pointer(object->type)) {
            return false;
        }
        *slot_type = object->type;
        return true;
    }
    if (location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_ARRAY_ELEMENT) {
        const MinicArrayType *array_type;

        if (!minic_type_is_array(object->type)) {
            return false;
        }
        array_type = minic_c0_program_array_type(program, object->type.array_type_id);
        /* Inferred-bound definitions append symbolic slots while the array type is
         * intentionally incomplete. Once the descriptor is complete, reject an
         * out-of-range slot immediately; final verification remains strict. */
        if (array_type == NULL ||
            (array_type->element_count != 0U && location_index >= array_type->element_count) ||
            !minic_type_is_pointer(array_type->element_type)) {
            return false;
        }
        *slot_type = array_type->element_type;
        return true;
    }
    if (location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_RECORD_FIELD) {
        const MinicRecord *record;
        const MinicRecordField *field;

        if (!minic_type_is_record(object->type)) {
            return false;
        }
        record = minic_c0_program_record(program, object->type.record_id);
        field = record == NULL ? NULL : minic_c0_record_field(record, location_index);
        if (field == NULL || field->element_count != 1U || field->is_bit_field ||
            field->is_flexible_array || !minic_type_is_pointer(field->type)) {
            return false;
        }
        *slot_type = field->type;
        return true;
    }
    if (location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR) {
        size_t remaining;

        remaining = location_index;
        return aggregate_scalar_slot_type(program, object->type, &remaining, slot_type) &&
               minic_type_is_pointer(*slot_type);
    }
    return false;
}

static bool add_global_symbol_relocation(MinicC0Program *program,
                                         MinicGlobalObjectId global_object_id,
                                         MinicGlobalRelocationLocationKind location_kind,
                                         size_t location_index,
                                         MinicGlobalRelocationTargetKind target_kind,
                                         size_t target_id,
                                         const size_t *target_member_indices,
                                         size_t target_member_depth,
                                         bool has_explicit_pointer_cast) {
    MinicGlobalObject *object;
    MinicGlobalRelocation *relocation;
    MinicType slot_pointee;
    MinicType slot_type;
    MinicType target_type;
    size_t path_index;

    if (program == NULL || global_object_id >= program->global_object_count ||
        target_member_depth > MINIC_GLOBAL_RELOCATION_MAX_MEMBER_DEPTH ||
        (target_member_depth != 0U && target_member_indices == NULL) ||
        (target_kind != MINIC_GLOBAL_RELOCATION_OBJECT &&
         target_kind != MINIC_GLOBAL_RELOCATION_FUNCTION) ||
        (target_kind == MINIC_GLOBAL_RELOCATION_OBJECT &&
         target_id >= program->global_object_count) ||
        (target_kind == MINIC_GLOBAL_RELOCATION_FUNCTION &&
         (target_id >= program->function_count || target_member_depth != 0U))) {
        return false;
    }
    object = &program->global_objects[global_object_id];
    if (!minic_c0_global_relocation_slot_type(
            program, object, location_kind, location_index, &slot_type) ||
        !minic_type_pointee(slot_type, &slot_pointee) ||
        (target_kind == MINIC_GLOBAL_RELOCATION_FUNCTION && !minic_type_is_function(slot_pointee) &&
         !has_explicit_pointer_cast) ||
        (target_kind == MINIC_GLOBAL_RELOCATION_OBJECT && minic_type_is_function(slot_pointee) &&
         !has_explicit_pointer_cast) ||
        (target_kind == MINIC_GLOBAL_RELOCATION_OBJECT &&
         (!global_object_member_path_type(program,
                                          &program->global_objects[target_id],
                                          target_member_indices,
                                          target_member_depth,
                                          &target_type) ||
          !global_relocation_object_target_type_compatible(
              program, slot_type, target_type, has_explicit_pointer_cast))) ||
        object->is_tentative ||
        (object->initializer_count != 0U &&
         ((location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_RECORD_FIELD &&
           (!minic_type_is_record(object->type) || location_index >= object->initializer_count ||
            object->initializer_values[location_index] != 0U)) ||
          (location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR &&
           (location_index >= object->initializer_count ||
            object->initializer_values[location_index] != 0U)) ||
          (location_kind != MINIC_GLOBAL_RELOCATION_LOCATION_RECORD_FIELD &&
           location_kind != MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR))) ||
        (object->relocation_count != 0U &&
         (object->relocations[object->relocation_count - 1U].location_kind != location_kind ||
          object->relocations[object->relocation_count - 1U].location_index >= location_index)) ||
        !grow_array((void **)&object->relocations,
                    &object->relocation_capacity,
                    object->relocation_count,
                    sizeof(*object->relocations))) {
        return false;
    }
    relocation = &object->relocations[object->relocation_count];
    (void)memset(relocation, 0, sizeof(*relocation));
    relocation->location_kind = location_kind;
    relocation->location_index = location_index;
    relocation->target_kind = target_kind;
    relocation->target_id = target_id;
    relocation->target_member_depth = target_member_depth;
    relocation->has_explicit_pointer_cast = has_explicit_pointer_cast;
    for (path_index = 0U; path_index < target_member_depth; ++path_index) {
        relocation->target_member_indices[path_index] = target_member_indices[path_index];
    }
    object->relocation_count += 1U;
    return true;
}

bool minic_c0_global_object_add_function_relocation(MinicC0Program *program,
                                                    MinicGlobalObjectId global_object_id,
                                                    MinicGlobalRelocationLocationKind location_kind,
                                                    size_t location_index,
                                                    MinicFunctionId function_id) {
    return add_global_symbol_relocation(program,
                                        global_object_id,
                                        location_kind,
                                        location_index,
                                        MINIC_GLOBAL_RELOCATION_FUNCTION,
                                        function_id,
                                        NULL,
                                        0U,
                                        false);
}

bool minic_c0_global_object_add_function_relocation_cast(
    MinicC0Program *program,
    MinicGlobalObjectId global_object_id,
    MinicGlobalRelocationLocationKind location_kind,
    size_t location_index,
    MinicFunctionId function_id) {
    return add_global_symbol_relocation(program,
                                        global_object_id,
                                        location_kind,
                                        location_index,
                                        MINIC_GLOBAL_RELOCATION_FUNCTION,
                                        function_id,
                                        NULL,
                                        0U,
                                        true);
}

bool minic_c0_global_object_set_extern(MinicC0Program *program,
                                       MinicGlobalObjectId global_object_id) {
    MinicGlobalObject *object;

    if (program == NULL || global_object_id >= program->global_object_count) {
        return false;
    }
    object = &program->global_objects[global_object_id];
    if (object->is_tentative || object->initializer_count != 0U || object->relocation_count != 0U ||
        object->is_zero_initialized || object->is_internal) {
        return false;
    }
    object->is_extern = true;
    return true;
}

bool minic_c0_global_object_add_object_relocation_path(
    MinicC0Program *program,
    MinicGlobalObjectId global_object_id,
    MinicGlobalRelocationLocationKind location_kind,
    size_t location_index,
    MinicGlobalObjectId target_object_id,
    const size_t *target_member_indices,
    size_t target_member_depth) {
    return add_global_symbol_relocation(program,
                                        global_object_id,
                                        location_kind,
                                        location_index,
                                        MINIC_GLOBAL_RELOCATION_OBJECT,
                                        target_object_id,
                                        target_member_indices,
                                        target_member_depth,
                                        false);
}

bool minic_c0_global_object_add_object_relocation_path_cast(
    MinicC0Program *program,
    MinicGlobalObjectId global_object_id,
    MinicGlobalRelocationLocationKind location_kind,
    size_t location_index,
    MinicGlobalObjectId target_object_id,
    const size_t *target_member_indices,
    size_t target_member_depth) {
    return add_global_symbol_relocation(program,
                                        global_object_id,
                                        location_kind,
                                        location_index,
                                        MINIC_GLOBAL_RELOCATION_OBJECT,
                                        target_object_id,
                                        target_member_indices,
                                        target_member_depth,
                                        true);
}

bool minic_c0_global_object_add_object_relocation(MinicC0Program *program,
                                                  MinicGlobalObjectId global_object_id,
                                                  MinicGlobalRelocationLocationKind location_kind,
                                                  size_t location_index,
                                                  MinicGlobalObjectId target_object_id) {
    return minic_c0_global_object_add_object_relocation_path(
        program, global_object_id, location_kind, location_index, target_object_id, NULL, 0U);
}

bool minic_c0_global_object_set_zero_initialized(MinicC0Program *program,
                                                 MinicGlobalObjectId global_object_id) {
    MinicGlobalObject *object;

    if (program == NULL || global_object_id >= program->global_object_count) {
        return false;
    }
    object = &program->global_objects[global_object_id];
    if (object->is_tentative || object->initializer_count != 0U) {
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
