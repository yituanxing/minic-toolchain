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
        if (!binding->is_local && binding->name_length == name_length &&
            memcmp(binding->name, name, name_length) == 0) {
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

static bool add_fixed_register_binding(MinicC0Program *program,
                                       const char *name,
                                       size_t name_length,
                                       MinicType type,
                                       const char *register_name,
                                       size_t register_name_length,
                                       MinicLocalId local_id,
                                       bool is_local,
                                       MinicFixedRegisterBindingId *binding_id) {
    MinicFixedRegisterBinding binding;

    if (program == NULL || name == NULL || register_name == NULL || binding_id == NULL ||
        register_name_length == 0U ||
        (!minic_type_is_integer(type) && !minic_type_is_pointer(type)) ||
        (!is_local && name_conflicts(program, name, name_length)) ||
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
    binding.local_id = local_id;
    binding.is_local = is_local;
    *binding_id = program->fixed_register_binding_count;
    program->fixed_register_bindings[program->fixed_register_binding_count] = binding;
    program->fixed_register_binding_count += 1U;
    return true;
}

const MinicFixedRegisterBinding *
minic_c0_program_local_fixed_register_binding(const MinicC0Program *program,
                                              MinicLocalId local_id) {
    size_t index;

    if (program == NULL || local_id >= program->local_count) {
        return NULL;
    }
    for (index = 0U; index < program->fixed_register_binding_count; ++index) {
        const MinicFixedRegisterBinding *binding;

        binding = &program->fixed_register_bindings[index];
        if (binding->is_local && binding->local_id == local_id) {
            return binding;
        }
    }
    return NULL;
}

bool minic_c0_program_add_fixed_register_binding(MinicC0Program *program,
                                                 const char *name,
                                                 size_t name_length,
                                                 MinicType type,
                                                 const char *register_name,
                                                 size_t register_name_length,
                                                 MinicFixedRegisterBindingId *binding_id) {
    return add_fixed_register_binding(program,
                                      name,
                                      name_length,
                                      type,
                                      register_name,
                                      register_name_length,
                                      MINIC_LOCAL_INVALID,
                                      false,
                                      binding_id);
}

bool minic_c0_program_add_local_fixed_register_binding(MinicC0Program *program,
                                                       MinicLocalId local_id,
                                                       const char *name,
                                                       size_t name_length,
                                                       const char *register_name,
                                                       size_t register_name_length,
                                                       MinicFixedRegisterBindingId *binding_id) {
    const MinicLocal *local;

    local = minic_c0_program_local(program, local_id);
    if (local == NULL || local->is_array ||
        minic_c0_program_local_fixed_register_binding(program, local_id) != NULL) {
        return false;
    }
    return add_fixed_register_binding(program,
                                      name,
                                      name_length,
                                      local->type,
                                      register_name,
                                      register_name_length,
                                      local_id,
                                      true,
                                      binding_id);
}

typedef struct MinicGlobalObjectInitialState {
    const char *section_name;
    size_t section_name_length;
    size_t explicit_alignment;
    MinicSymbolVisibility visibility;
    bool is_internal;
    bool is_read_only;
    bool is_extern;
    bool is_weak;
    bool is_block_scope_extern_only;
} MinicGlobalObjectInitialState;

static bool global_object_initial_state_valid(const MinicGlobalObjectInitialState *state) {
    size_t alignment;

    if (state == NULL || ((state->section_name == NULL) != (state->section_name_length == 0U)) ||
        state->section_name_length == SIZE_MAX ||
        state->visibility < MINIC_SYMBOL_VISIBILITY_DEFAULT ||
        state->visibility > MINIC_SYMBOL_VISIBILITY_PROTECTED ||
        (state->is_weak && state->is_internal)) {
        return false;
    }
    alignment = state->explicit_alignment;
    return alignment == 0U || (alignment & (alignment - 1U)) == 0U;
}

static bool add_global_object_entity_with_state(MinicC0Program *program,
                                                const char *name,
                                                size_t name_length,
                                                MinicType type,
                                                const MinicGlobalObjectInitialState *state,
                                                MinicGlobalObjectId *global_object_id) {
    MinicGlobalObject object;

    if (program == NULL || name == NULL || global_object_id == NULL ||
        !global_object_initial_state_valid(state) ||
        (minic_type_is_void(type) && !state->is_extern) ||
        name_conflicts(program, name, name_length)) {
        return false;
    }

    (void)memset(&object, 0, sizeof(object));
    object.name = copy_name(name, name_length);
    if (object.name == NULL) {
        return false;
    }
    if (state->section_name != NULL) {
        object.section_name = copy_name(state->section_name, state->section_name_length);
        if (object.section_name == NULL) {
            free(object.name);
            return false;
        }
    }
    if (!grow_array((void **)&program->global_objects,
                    &program->global_object_capacity,
                    program->global_object_count,
                    sizeof(*program->global_objects))) {
        free(object.section_name);
        free(object.name);
        return false;
    }

    object.name_length = name_length;
    object.section_name_length = state->section_name_length;
    object.type = type;
    object.explicit_alignment = state->explicit_alignment;
    object.visibility = state->visibility;
    object.is_internal = state->is_internal;
    object.is_weak = state->is_weak;
    object.is_read_only = state->is_read_only;
    object.is_extern = state->is_extern;
    object.is_block_scope_extern_only = state->is_block_scope_extern_only;
    *global_object_id = program->global_object_count;
    program->global_objects[program->global_object_count] = object;
    program->global_object_count += 1U;
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
    MinicGlobalObjectInitialState state;

    (void)memset(&state, 0, sizeof(state));
    state.visibility = MINIC_SYMBOL_VISIBILITY_DEFAULT;
    state.is_internal = is_internal;
    state.is_read_only = is_read_only;
    state.is_extern = is_extern;
    return add_global_object_entity_with_state(
        program, name, name_length, type, &state, global_object_id);
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

bool minic_c0_program_add_extern_global_object_with_metadata(
    MinicC0Program *program,
    const char *name,
    size_t name_length,
    MinicType type,
    bool is_read_only,
    const char *section_name,
    size_t section_name_length,
    size_t explicit_alignment,
    MinicSymbolVisibility visibility,
    bool is_weak,
    bool is_block_scope_extern_only,
    MinicGlobalObjectId *global_object_id) {
    MinicGlobalObjectInitialState state;

    (void)memset(&state, 0, sizeof(state));
    state.section_name = section_name;
    state.section_name_length = section_name_length;
    state.explicit_alignment = explicit_alignment;
    state.visibility = visibility;
    state.is_read_only = is_read_only;
    state.is_extern = true;
    state.is_weak = is_weak;
    state.is_block_scope_extern_only = is_block_scope_extern_only;
    return add_global_object_entity_with_state(
        program, name, name_length, type, &state, global_object_id);
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

bool minic_c0_global_object_replace_zero_initializer_bits(MinicC0Program *program,
                                                          MinicGlobalObjectId global_object_id,
                                                          size_t initializer_index,
                                                          uint64_t bits) {
    MinicGlobalObject *object;
    size_t relocation_index;

    if (program == NULL || global_object_id >= program->global_object_count) {
        return false;
    }
    object = &program->global_objects[global_object_id];
    if (object->is_tentative || object->is_zero_initialized ||
        initializer_index >= object->initializer_count ||
        object->initializer_values[initializer_index] != 0U) {
        return false;
    }
    for (relocation_index = 0U; relocation_index < object->relocation_count; ++relocation_index) {
        const MinicGlobalRelocation *relocation;

        relocation = &object->relocations[relocation_index];
        if (relocation->location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR &&
            relocation->location_index == initializer_index) {
            return false;
        }
    }
    object->initializer_values[initializer_index] = bits;
    return true;
}

bool minic_c0_global_object_add_initializer(MinicC0Program *program,
                                            MinicGlobalObjectId global_object_id,
                                            int value) {
    return minic_c0_global_object_add_initializer_bits(
        program, global_object_id, (uint64_t)(int64_t)value);
}

bool minic_c0_global_initializer_slot_count(const MinicC0Program *program,
                                            MinicType type,
                                            size_t *slot_count) {
    return minic_c0_type_initializer_slot_count(program, type, slot_count);
}

bool minic_c0_global_object_union_member_selection(const MinicC0Program *program,
                                                   const MinicGlobalObject *object,
                                                   size_t initializer_slot,
                                                   MinicRecordId record_id,
                                                   size_t *field_index) {
    size_t index;

    if (program == NULL || object == NULL || field_index == NULL ||
        record_id >= program->record_count) {
        return false;
    }
    for (index = 0U; index < object->union_selection_count; ++index) {
        const MinicGlobalUnionSelection *selection;

        selection = &object->union_selections[index];
        if (selection->initializer_slot == initializer_slot && selection->record_id == record_id) {
            *field_index = selection->field_index;
            return true;
        }
    }
    return false;
}

bool minic_c0_global_object_union_member_initializer_span(const MinicC0Program *program,
                                                          const MinicGlobalObject *object,
                                                          size_t initializer_slot,
                                                          MinicRecordId record_id,
                                                          size_t *initializer_span) {
    size_t index;

    if (program == NULL || object == NULL || initializer_span == NULL ||
        record_id >= program->record_count) {
        return false;
    }
    for (index = 0U; index < object->union_selection_count; ++index) {
        const MinicGlobalUnionSelection *selection;

        selection = &object->union_selections[index];
        if (selection->initializer_slot == initializer_slot && selection->record_id == record_id) {
            *initializer_span = selection->initializer_span;
            return true;
        }
    }
    return false;
}

bool minic_c0_global_object_select_union_member_with_span(MinicC0Program *program,
                                                          MinicGlobalObjectId global_object_id,
                                                          size_t initializer_slot,
                                                          MinicRecordId record_id,
                                                          size_t field_index,
                                                          size_t initializer_span) {
    MinicGlobalObject *object;
    const MinicRecord *record;
    const MinicRecordField *field;
    size_t element_slots;
    size_t selected_slots;
    size_t index;

    if (program == NULL || global_object_id >= program->global_object_count ||
        record_id >= program->record_count) {
        return false;
    }
    record = minic_c0_program_record(program, record_id);
    field = record != NULL ? minic_c0_record_field(record, field_index) : NULL;
    if (record == NULL || !record->is_complete || !record->is_union || field == NULL ||
        field->element_count == 0U ||
        !minic_c0_global_initializer_slot_count(program, field->type, &element_slots) ||
        (element_slots != 0U && field->element_count > SIZE_MAX / element_slots)) {
        return false;
    }
    selected_slots = field->element_count * element_slots;
    object = &program->global_objects[global_object_id];
    if (initializer_span != 0U &&
        (selected_slots > initializer_span || initializer_slot > object->initializer_count ||
         initializer_span > object->initializer_count - initializer_slot)) {
        return false;
    }
    for (index = 0U; index < object->union_selection_count; ++index) {
        MinicGlobalUnionSelection *selection;

        selection = &object->union_selections[index];
        if (selection->initializer_slot == initializer_slot && selection->record_id == record_id) {
            selection->field_index = field_index;
            selection->initializer_span = initializer_span;
            return true;
        }
    }
    if (!grow_array((void **)&object->union_selections,
                    &object->union_selection_capacity,
                    object->union_selection_count,
                    sizeof(*object->union_selections))) {
        return false;
    }
    object->union_selections[object->union_selection_count].initializer_slot = initializer_slot;
    object->union_selections[object->union_selection_count].initializer_span = initializer_span;
    object->union_selections[object->union_selection_count].record_id = record_id;
    object->union_selections[object->union_selection_count].field_index = field_index;
    object->union_selection_count += 1U;
    return true;
}

bool minic_c0_global_object_select_union_member(MinicC0Program *program,
                                                MinicGlobalObjectId global_object_id,
                                                size_t initializer_slot,
                                                MinicRecordId record_id,
                                                size_t field_index) {
    return minic_c0_global_object_select_union_member_with_span(
        program, global_object_id, initializer_slot, record_id, field_index, 0U);
}

bool minic_c0_global_record_field_initializer_slot(const MinicC0Program *program,
                                                   const MinicRecord *record,
                                                   size_t field_index,
                                                   size_t *slot_index) {
    size_t index;
    size_t total;

    if (program == NULL || record == NULL || slot_index == NULL || !record->is_complete ||
        field_index >= record->field_count || (record->is_union && field_index != 0U)) {
        return false;
    }
    total = 0U;
    for (index = 0U; index < field_index; ++index) {
        const MinicRecordField *field;
        size_t element_slots;
        size_t field_slots;

        field = &record->fields[index];
        if (field->is_flexible_array || field->is_zero_length_array) {
            continue;
        }
        if (field->element_count == 0U ||
            !minic_c0_type_initializer_slot_count(program, field->type, &element_slots) ||
            (element_slots != 0U && field->element_count > SIZE_MAX / element_slots)) {
            return false;
        }
        field_slots = field->element_count * element_slots;
        if (total > SIZE_MAX - field_slots) {
            return false;
        }
        total += field_slots;
    }
    *slot_index = total;
    return true;
}

static bool global_object_type_initializer_slot_count_at(const MinicC0Program *program,
                                                         const MinicGlobalObject *object,
                                                         MinicType type,
                                                         size_t base_slot,
                                                         size_t *slot_count) {
    size_t total;

    if (program == NULL || object == NULL || slot_count == NULL) {
        return false;
    }
    if (minic_type_is_integer(type) || minic_type_is_pointer(type)) {
        *slot_count = 1U;
        return true;
    }
    if (minic_type_is_array(type)) {
        const MinicArrayType *array_type;
        size_t element_index;

        array_type = minic_c0_program_array_type(program, type.array_type_id);
        if (array_type == NULL || array_type->element_count == 0U) {
            return false;
        }
        total = 0U;
        for (element_index = 0U; element_index < array_type->element_count; ++element_index) {
            size_t child_count;

            if (!global_object_type_initializer_slot_count_at(
                    program, object, array_type->element_type, base_slot + total, &child_count) ||
                total > SIZE_MAX - child_count) {
                return false;
            }
            total += child_count;
        }
        *slot_count = total;
        return true;
    }
    if (minic_type_is_record(type)) {
        const MinicRecord *record;
        size_t field_begin;
        size_t field_end;
        size_t field_index;

        record = minic_c0_program_record(program, type.record_id);
        if (record == NULL || !record->is_complete) {
            return false;
        }
        field_begin = 0U;
        field_end = record->field_count;
        if (record->is_union) {
            size_t selected;

            selected = 0U;
            (void)minic_c0_global_object_union_member_selection(
                program, object, base_slot, type.record_id, &selected);
            if (selected >= record->field_count) {
                return false;
            }
            field_begin = selected;
            field_end = selected + 1U;
        }
        total = 0U;
        for (field_index = field_begin; field_index < field_end; ++field_index) {
            const MinicRecordField *field;
            size_t element_index;
            size_t element_count;

            field = minic_c0_record_field(record, field_index);
            if (field == NULL || field->element_count == 0U) {
                return false;
            }
            if (field->is_zero_length_array) {
                continue;
            }
            element_count = field->element_count;
            if (field->is_flexible_array) {
                if (base_slot != 0U || !minic_type_equal(type, object->type)) {
                    continue;
                }
                element_count = object->flexible_array_initializer_count;
            }
            for (element_index = 0U; element_index < element_count; ++element_index) {
                size_t child_count;

                if (!global_object_type_initializer_slot_count_at(
                        program, object, field->type, base_slot + total, &child_count) ||
                    total > SIZE_MAX - child_count) {
                    return false;
                }
                total += child_count;
            }
        }
        if (record->is_union) {
            size_t initializer_span;

            initializer_span = 0U;
            if (minic_c0_global_object_union_member_initializer_span(
                    program, object, base_slot, type.record_id, &initializer_span) &&
                initializer_span != 0U) {
                if (total > initializer_span) {
                    return false;
                }
                total = initializer_span;
            }
        }
        *slot_count = total;
        return true;
    }
    return false;
}

static bool aggregate_scalar_slot_type_for_object(const MinicC0Program *program,
                                                  const MinicGlobalObject *object,
                                                  MinicType type,
                                                  size_t base_slot,
                                                  size_t target_slot,
                                                  MinicType *slot_type) {
    if (program == NULL || object == NULL || slot_type == NULL || target_slot < base_slot) {
        return false;
    }
    if (minic_type_is_integer(type) || minic_type_is_pointer(type)) {
        if (target_slot != base_slot) {
            return false;
        }
        *slot_type = type;
        return true;
    }
    if (minic_type_is_array(type)) {
        const MinicArrayType *array_type;
        size_t cursor;
        size_t element_index;

        array_type = minic_c0_program_array_type(program, type.array_type_id);
        if (array_type == NULL) {
            return false;
        }
        if (array_type->element_count == 0U && !array_type->is_zero_length) {
            size_t child_count;
            size_t selected;

            if (!minic_c0_type_initializer_slot_count(
                    program, array_type->element_type, &child_count) ||
                child_count == 0U) {
                return false;
            }
            selected = (target_slot - base_slot) / child_count;
            if (selected > (SIZE_MAX - base_slot) / child_count) {
                return false;
            }
            return aggregate_scalar_slot_type_for_object(program,
                                                         object,
                                                         array_type->element_type,
                                                         base_slot + selected * child_count,
                                                         target_slot,
                                                         slot_type);
        }
        if (array_type->element_count == 0U) {
            return false;
        }
        cursor = base_slot;
        for (element_index = 0U; element_index < array_type->element_count; ++element_index) {
            size_t child_count;

            if (!global_object_type_initializer_slot_count_at(
                    program, object, array_type->element_type, cursor, &child_count)) {
                return false;
            }
            if (target_slot >= cursor && target_slot - cursor < child_count) {
                return aggregate_scalar_slot_type_for_object(
                    program, object, array_type->element_type, cursor, target_slot, slot_type);
            }
            if (cursor > SIZE_MAX - child_count) {
                return false;
            }
            cursor += child_count;
        }
        return false;
    }
    if (minic_type_is_record(type)) {
        const MinicRecord *record;
        size_t cursor;
        size_t field_begin;
        size_t field_end;
        size_t field_index;

        record = minic_c0_program_record(program, type.record_id);
        if (record == NULL || !record->is_complete) {
            return false;
        }
        field_begin = 0U;
        field_end = record->field_count;
        if (record->is_union) {
            size_t selected;

            selected = 0U;
            (void)minic_c0_global_object_union_member_selection(
                program, object, base_slot, type.record_id, &selected);
            if (selected >= record->field_count) {
                return false;
            }
            field_begin = selected;
            field_end = selected + 1U;
        }
        cursor = base_slot;
        for (field_index = field_begin; field_index < field_end; ++field_index) {
            const MinicRecordField *field;
            size_t element_count;
            size_t element_index;

            field = minic_c0_record_field(record, field_index);
            if (field == NULL || field->element_count == 0U) {
                return false;
            }
            if (field->is_zero_length_array) {
                continue;
            }
            element_count = field->element_count;
            if (field->is_flexible_array) {
                if (base_slot != 0U || !minic_type_equal(type, object->type)) {
                    continue;
                }
                element_count = object->flexible_array_initializer_count;
            }
            for (element_index = 0U; element_index < element_count; ++element_index) {
                size_t child_count;

                if (!global_object_type_initializer_slot_count_at(
                        program, object, field->type, cursor, &child_count)) {
                    return false;
                }
                if (target_slot >= cursor && target_slot - cursor < child_count) {
                    return aggregate_scalar_slot_type_for_object(
                        program, object, field->type, cursor, target_slot, slot_type);
                }
                if (cursor > SIZE_MAX - child_count) {
                    return false;
                }
                cursor += child_count;
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
        if (field == NULL || field->element_count == 0U || field->is_bit_field ||
            field->is_flexible_array || (field->is_array && depth + 1U != member_depth) ||
            (!field->is_array && field->element_count != 1U)) {
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

static bool global_relocation_pointer_target_type_compatible(MinicType slot_type,
                                                             MinicType source_pointer_type) {
    return minic_type_assignment_compatible(slot_type, source_pointer_type) ||
           minic_type_gnu_pointer_sign_compatible(slot_type, source_pointer_type);
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
        global_relocation_pointer_target_type_compatible(slot_type, source_pointer_type)) {
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
            global_relocation_pointer_target_type_compatible(slot_type, source_pointer_type)) {
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

bool minic_c0_global_relocation_function_target_compatible(const MinicC0Program *program,
                                                           MinicType slot_type,
                                                           MinicFunctionId function_id,
                                                           bool has_explicit_pointer_cast) {
    MinicType slot_pointee;

    return program != NULL && function_id < program->function_count &&
           minic_type_is_pointer(slot_type) && minic_type_pointee(slot_type, &slot_pointee) &&
           (has_explicit_pointer_cast || minic_type_is_function(slot_pointee) ||
            minic_type_is_void(slot_pointee));
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
        return aggregate_scalar_slot_type_for_object(
                   program, object, object->type, 0U, location_index, slot_type) &&
               (minic_type_is_pointer(*slot_type) || minic_type_is_integer(*slot_type));
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
                                         int64_t target_byte_addend,
                                         bool has_explicit_pointer_cast) {
    MinicGlobalObject *object;
    MinicGlobalRelocation *relocation;
    MinicType slot_pointee;
    MinicType slot_type;
    MinicType target_type;
    bool slot_is_integer;
    bool slot_is_pointer;
    size_t insertion_index;
    size_t path_index;

    if (program == NULL || global_object_id >= program->global_object_count ||
        target_member_depth > MINIC_GLOBAL_RELOCATION_MAX_MEMBER_DEPTH ||
        (target_member_depth != 0U && target_member_indices == NULL) ||
        (target_kind != MINIC_GLOBAL_RELOCATION_OBJECT &&
         target_kind != MINIC_GLOBAL_RELOCATION_FUNCTION &&
         target_kind != MINIC_GLOBAL_RELOCATION_LABEL) ||
        (target_kind == MINIC_GLOBAL_RELOCATION_OBJECT &&
         target_id >= program->global_object_count) ||
        (target_kind == MINIC_GLOBAL_RELOCATION_FUNCTION &&
         (target_id >= program->function_count || target_member_depth != 0U ||
          target_byte_addend != 0)) ||
        (target_kind == MINIC_GLOBAL_RELOCATION_LABEL &&
         (target_id >= program->statement_count ||
          program->statements[target_id].kind != MINIC_STATEMENT_LABEL ||
          target_member_depth != 0U || target_byte_addend != 0 || has_explicit_pointer_cast))) {
        return false;
    }
    object = &program->global_objects[global_object_id];
    if (!minic_c0_global_relocation_slot_type(
            program, object, location_kind, location_index, &slot_type)) {
        return false;
    }
    slot_is_pointer = minic_type_is_pointer(slot_type);
    slot_is_integer = minic_type_is_integer(slot_type);
    if ((!slot_is_pointer && !slot_is_integer) ||
        (slot_is_pointer && !minic_type_pointee(slot_type, &slot_pointee)) ||
        (target_kind == MINIC_GLOBAL_RELOCATION_FUNCTION && slot_is_pointer &&
         !minic_c0_global_relocation_function_target_compatible(
             program, slot_type, (MinicFunctionId)target_id, has_explicit_pointer_cast)) ||
        (target_kind == MINIC_GLOBAL_RELOCATION_LABEL && !slot_is_pointer) ||
        (target_kind == MINIC_GLOBAL_RELOCATION_OBJECT && slot_is_pointer &&
         minic_type_is_function(slot_pointee) && !has_explicit_pointer_cast) ||
        (target_kind == MINIC_GLOBAL_RELOCATION_OBJECT &&
         (!global_object_member_path_type(program,
                                          &program->global_objects[target_id],
                                          target_member_indices,
                                          target_member_depth,
                                          &target_type) ||
          (slot_is_pointer && !global_relocation_object_target_type_compatible(
                                  program, slot_type, target_type, has_explicit_pointer_cast)))) ||
        object->is_tentative ||
        (object->initializer_count != 0U &&
         ((location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_RECORD_FIELD &&
           (!minic_type_is_record(object->type) || location_index >= object->initializer_count ||
            object->initializer_values[location_index] != 0U)) ||
          (location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR &&
           (location_index >= object->initializer_count ||
            object->initializer_values[location_index] != 0U)) ||
          (location_kind != MINIC_GLOBAL_RELOCATION_LOCATION_RECORD_FIELD &&
           location_kind != MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR)))) {
        return false;
    }
    insertion_index = 0U;
    while (insertion_index < object->relocation_count) {
        const MinicGlobalRelocation *existing;

        existing = &object->relocations[insertion_index];
        if (existing->location_kind != location_kind ||
            existing->location_index == location_index) {
            return false;
        }
        if (existing->location_index > location_index) {
            break;
        }
        insertion_index += 1U;
    }
    if (!grow_array((void **)&object->relocations,
                    &object->relocation_capacity,
                    object->relocation_count,
                    sizeof(*object->relocations))) {
        return false;
    }
    if (insertion_index < object->relocation_count) {
        (void)memmove(&object->relocations[insertion_index + 1U],
                      &object->relocations[insertion_index],
                      (object->relocation_count - insertion_index) * sizeof(*object->relocations));
    }
    relocation = &object->relocations[insertion_index];
    (void)memset(relocation, 0, sizeof(*relocation));
    relocation->location_kind = location_kind;
    relocation->location_index = location_index;
    relocation->target_kind = target_kind;
    relocation->target_id = target_id;
    relocation->target_member_depth = target_member_depth;
    relocation->target_byte_addend = target_byte_addend;
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
                                        0,
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
                                        0,
                                        true);
}

bool minic_c0_global_object_add_label_relocation(MinicC0Program *program,
                                                 MinicGlobalObjectId global_object_id,
                                                 MinicGlobalRelocationLocationKind location_kind,
                                                 size_t location_index,
                                                 MinicStatementId label_statement_id) {
    return add_global_symbol_relocation(program,
                                        global_object_id,
                                        location_kind,
                                        location_index,
                                        MINIC_GLOBAL_RELOCATION_LABEL,
                                        label_statement_id,
                                        NULL,
                                        0U,
                                        0,
                                        false);
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

bool minic_c0_global_object_add_object_relocation_path_addend(
    MinicC0Program *program,
    MinicGlobalObjectId global_object_id,
    MinicGlobalRelocationLocationKind location_kind,
    size_t location_index,
    MinicGlobalObjectId target_object_id,
    const size_t *target_member_indices,
    size_t target_member_depth,
    int64_t target_byte_addend) {
    return add_global_symbol_relocation(program,
                                        global_object_id,
                                        location_kind,
                                        location_index,
                                        MINIC_GLOBAL_RELOCATION_OBJECT,
                                        target_object_id,
                                        target_member_indices,
                                        target_member_depth,
                                        target_byte_addend,
                                        false);
}

bool minic_c0_global_object_add_object_relocation_path_addend_cast(
    MinicC0Program *program,
    MinicGlobalObjectId global_object_id,
    MinicGlobalRelocationLocationKind location_kind,
    size_t location_index,
    MinicGlobalObjectId target_object_id,
    const size_t *target_member_indices,
    size_t target_member_depth,
    int64_t target_byte_addend) {
    return add_global_symbol_relocation(program,
                                        global_object_id,
                                        location_kind,
                                        location_index,
                                        MINIC_GLOBAL_RELOCATION_OBJECT,
                                        target_object_id,
                                        target_member_indices,
                                        target_member_depth,
                                        target_byte_addend,
                                        true);
}

bool minic_c0_global_object_add_integer_object_relocation_path_addend(
    MinicC0Program *program,
    MinicGlobalObjectId global_object_id,
    MinicGlobalRelocationLocationKind location_kind,
    size_t location_index,
    MinicGlobalObjectId target_object_id,
    const size_t *target_member_indices,
    size_t target_member_depth,
    int64_t target_byte_addend) {
    MinicType slot_type;

    if (program == NULL || global_object_id >= program->global_object_count ||
        !minic_c0_global_relocation_slot_type(program,
                                              &program->global_objects[global_object_id],
                                              location_kind,
                                              location_index,
                                              &slot_type) ||
        !minic_type_is_integer(slot_type)) {
        return false;
    }
    return add_global_symbol_relocation(program,
                                        global_object_id,
                                        location_kind,
                                        location_index,
                                        MINIC_GLOBAL_RELOCATION_OBJECT,
                                        target_object_id,
                                        target_member_indices,
                                        target_member_depth,
                                        target_byte_addend,
                                        false);
}

bool minic_c0_global_object_add_object_relocation_path(
    MinicC0Program *program,
    MinicGlobalObjectId global_object_id,
    MinicGlobalRelocationLocationKind location_kind,
    size_t location_index,
    MinicGlobalObjectId target_object_id,
    const size_t *target_member_indices,
    size_t target_member_depth) {
    return minic_c0_global_object_add_object_relocation_path_addend(program,
                                                                    global_object_id,
                                                                    location_kind,
                                                                    location_index,
                                                                    target_object_id,
                                                                    target_member_indices,
                                                                    target_member_depth,
                                                                    0);
}

bool minic_c0_global_object_add_object_relocation_path_cast(
    MinicC0Program *program,
    MinicGlobalObjectId global_object_id,
    MinicGlobalRelocationLocationKind location_kind,
    size_t location_index,
    MinicGlobalObjectId target_object_id,
    const size_t *target_member_indices,
    size_t target_member_depth) {
    return minic_c0_global_object_add_object_relocation_path_addend_cast(program,
                                                                         global_object_id,
                                                                         location_kind,
                                                                         location_index,
                                                                         target_object_id,
                                                                         target_member_indices,
                                                                         target_member_depth,
                                                                         0);
}

bool minic_c0_global_object_add_object_relocation(MinicC0Program *program,
                                                  MinicGlobalObjectId global_object_id,
                                                  MinicGlobalRelocationLocationKind location_kind,
                                                  size_t location_index,
                                                  MinicGlobalObjectId target_object_id) {
    return minic_c0_global_object_add_object_relocation_path(
        program, global_object_id, location_kind, location_index, target_object_id, NULL, 0U);
}

bool minic_c0_global_object_set_flexible_array_initializer_count(
    MinicC0Program *program, MinicGlobalObjectId global_object_id, size_t element_count) {
    MinicGlobalObject *object;
    const MinicRecord *record;
    const MinicRecordField *field;

    if (program == NULL || global_object_id >= program->global_object_count ||
        element_count == 0U) {
        return false;
    }
    object = &program->global_objects[global_object_id];
    if (object->is_extern || object->is_tentative || object->is_zero_initialized ||
        object->flexible_array_initializer_count != 0U || !minic_type_is_record(object->type)) {
        return false;
    }
    record = minic_c0_program_record(program, object->type.record_id);
    if (record == NULL || !record->is_complete || record->is_union || record->field_count == 0U) {
        return false;
    }
    field = &record->fields[record->field_count - 1U];
    if (!field->is_flexible_array || field->is_bit_field) {
        return false;
    }
    object->flexible_array_initializer_count = element_count;
    return true;
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

bool minic_c0_global_object_set_weak(MinicC0Program *program,
                                     MinicGlobalObjectId global_object_id,
                                     bool is_weak) {
    MinicGlobalObject *object;

    if (program == NULL || global_object_id >= program->global_object_count) {
        return false;
    }
    object = &program->global_objects[global_object_id];
    if (is_weak && object->is_internal) {
        return false;
    }
    object->is_weak = is_weak;
    return true;
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
