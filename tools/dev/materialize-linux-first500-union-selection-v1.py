#!/usr/bin/env python3
"""Materialize persistent active-union selection for static aggregate objects."""
from pathlib import Path


def replace_once(path: Path, before: str, after: str) -> None:
    text = path.read_text()
    if after in text:
        return
    count = text.count(before)
    if count != 1:
        raise SystemExit(f"{path}: expected one anchor, found {count}")
    path.write_text(text.replace(before, after, 1))


def replace_between(path: Path, start: str, end: str, replacement: str) -> None:
    text = path.read_text()
    begin = text.find(start)
    if begin < 0:
        if replacement.strip() in text:
            return
        raise SystemExit(f"{path}: missing start marker {start!r}")
    finish = text.find(end, begin + len(start))
    if finish < 0:
        raise SystemExit(f"{path}: missing end marker {end!r}")
    path.write_text(text[:begin] + replacement + "\n\n" + text[finish:])


ast = Path("src/frontend/ast.h")
replace_once(
    ast,
    """typedef struct MinicGlobalObject {
""",
    """typedef struct MinicGlobalUnionSelection {
    size_t initializer_slot;
    MinicRecordId record_id;
    size_t field_index;
} MinicGlobalUnionSelection;

typedef struct MinicGlobalObject {
""",
)
replace_once(
    ast,
    """    MinicGlobalRelocation *relocations;
    size_t relocation_count;
    size_t relocation_capacity;
    size_t flexible_array_initializer_count;
""",
    """    MinicGlobalRelocation *relocations;
    size_t relocation_count;
    size_t relocation_capacity;
    MinicGlobalUnionSelection *union_selections;
    size_t union_selection_count;
    size_t union_selection_capacity;
    size_t flexible_array_initializer_count;
""",
)
replace_once(
    ast,
    """bool minic_c0_global_initializer_slot_count(const MinicC0Program *program,
                                            MinicType type,
                                            size_t *slot_count);
bool minic_c0_global_record_field_initializer_slot(const MinicC0Program *program,
""",
    """bool minic_c0_global_initializer_slot_count(const MinicC0Program *program,
                                            MinicType type,
                                            size_t *slot_count);
bool minic_c0_global_object_select_union_member(MinicC0Program *program,
                                                MinicGlobalObjectId global_object_id,
                                                size_t initializer_slot,
                                                MinicRecordId record_id,
                                                size_t field_index);
bool minic_c0_global_object_union_member_selection(const MinicC0Program *program,
                                                    const MinicGlobalObject *object,
                                                    size_t initializer_slot,
                                                    MinicRecordId record_id,
                                                    size_t *field_index);
bool minic_c0_global_record_field_initializer_slot(const MinicC0Program *program,
""",
)

ast_c = Path("src/frontend/ast.c")
replace_once(
    ast_c,
    """        free(program->global_objects[index].initializer_values);
        free(program->global_objects[index].relocations);
""",
    """        free(program->global_objects[index].initializer_values);
        free(program->global_objects[index].relocations);
        free(program->global_objects[index].union_selections);
""",
)

ast_global = Path("src/frontend/ast_global.c")
replace_once(
    ast_global,
    """bool minic_c0_global_initializer_slot_count(const MinicC0Program *program,
                                            MinicType type,
                                            size_t *slot_count) {
    return minic_c0_type_initializer_slot_count(program, type, slot_count);
}

bool minic_c0_global_record_field_initializer_slot""",
    """bool minic_c0_global_initializer_slot_count(const MinicC0Program *program,
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
        if (selection->initializer_slot == initializer_slot &&
            selection->record_id == record_id) {
            *field_index = selection->field_index;
            return true;
        }
    }
    return false;
}

bool minic_c0_global_object_select_union_member(MinicC0Program *program,
                                                MinicGlobalObjectId global_object_id,
                                                size_t initializer_slot,
                                                MinicRecordId record_id,
                                                size_t field_index) {
    MinicGlobalObject *object;
    const MinicRecord *record;
    size_t index;

    if (program == NULL || global_object_id >= program->global_object_count ||
        record_id >= program->record_count) {
        return false;
    }
    record = minic_c0_program_record(program, record_id);
    if (record == NULL || !record->is_complete || !record->is_union ||
        field_index >= record->field_count) {
        return false;
    }
    object = &program->global_objects[global_object_id];
    for (index = 0U; index < object->union_selection_count; ++index) {
        MinicGlobalUnionSelection *selection;

        selection = &object->union_selections[index];
        if (selection->initializer_slot == initializer_slot &&
            selection->record_id == record_id) {
            selection->field_index = field_index;
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
    object->union_selections[object->union_selection_count].record_id = record_id;
    object->union_selections[object->union_selection_count].field_index = field_index;
    object->union_selection_count += 1U;
    return true;
}

bool minic_c0_global_record_field_initializer_slot""",
)

new_slot_helpers = r'''static bool global_object_type_initializer_slot_count_at(const MinicC0Program *program,
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

            if (!global_object_type_initializer_slot_count_at(program,
                                                              object,
                                                              array_type->element_type,
                                                              base_slot + total,
                                                              &child_count) ||
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

                if (!global_object_type_initializer_slot_count_at(program,
                                                                  object,
                                                                  field->type,
                                                                  base_slot + total,
                                                                  &child_count) ||
                    total > SIZE_MAX - child_count) {
                    return false;
                }
                total += child_count;
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
                    program, array_type->element_type, &child_count) || child_count == 0U) {
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
                return aggregate_scalar_slot_type_for_object(program,
                                                             object,
                                                             array_type->element_type,
                                                             cursor,
                                                             target_slot,
                                                             slot_type);
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
}'''
replace_between(
    ast_global,
    "static bool aggregate_scalar_slot_type(",
    "static bool global_object_member_path_type(",
    new_slot_helpers,
)
replace_once(
    ast_global,
    """    if (location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR) {
        size_t remaining;

        remaining = location_index;
        return aggregate_scalar_slot_type(program, object->type, &remaining, slot_type) &&
               (minic_type_is_pointer(*slot_type) || minic_type_is_integer(*slot_type));
    }
""",
    """    if (location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR) {
        return aggregate_scalar_slot_type_for_object(
                   program, object, object->type, 0U, location_index, slot_type) &&
               (minic_type_is_pointer(*slot_type) || minic_type_is_integer(*slot_type));
    }
""",
)

parser = Path("src/frontend/parser_global.c")
replace_once(
    parser,
    """static bool try_overwrite_static_zero_noncanonical_union_designator(
""",
    """static bool overwrite_static_zero_field_value(MinicParser *parser,
                                              MinicGlobalObjectId object_id,
                                              const MinicRecordField *field,
                                              size_t field_base_slot);

static bool try_overwrite_static_zero_noncanonical_union_designator(
""",
)

try_union = r'''static bool try_overwrite_static_zero_noncanonical_union_designator(
    MinicParser *parser,
    MinicGlobalObjectId object_id,
    const MinicRecord *record,
    const MinicStaticRecordDesignator *designator,
    size_t record_base_slot,
    bool *handled) {
    const MinicRecord *current_record;
    const MinicGlobalObject *object;
    size_t depth;
    size_t total;

    if (parser == NULL || record == NULL || designator == NULL || handled == NULL ||
        designator->depth == 0U || object_id >= parser->program->global_object_count) {
        return false;
    }
    *handled = false;
    current_record = record;
    object = &parser->program->global_objects[object_id];
    total = 0U;
    for (depth = 0U; depth < designator->depth; ++depth) {
        const MinicRecordField *field;
        size_t field_index;

        field_index = designator->field_indices[depth];
        field = minic_c0_record_field(current_record, field_index);
        if (field == NULL || field->element_count != 1U || field->is_array || field->is_bit_field ||
            field->is_flexible_array) {
            return true;
        }
        if (current_record->is_union && field_index != 0U) {
            const MinicRecordField *canonical_field;
            MinicRecordId union_record_id;
            size_t canonical_element_slots;
            size_t canonical_slots;
            size_t selected_element_slots;
            size_t selected_slots;
            size_t slot_begin;
            size_t slot_end;
            size_t slot;
            size_t relocation_index;

            *handled = true;
            if (depth + 1U != designator->depth || current_record->field_count == 0U ||
                current_record < parser->program->records ||
                current_record >= parser->program->records + parser->program->record_count) {
                minic_parser_error(parser,
                                   "backward noncanonical static union designator requires a "
                                   "direct active member");
                return false;
            }
            canonical_field = &current_record->fields[0];
            if (canonical_field->element_count == 0U || canonical_field->is_flexible_array ||
                !minic_c0_global_initializer_slot_count(
                    parser->program, canonical_field->type, &canonical_element_slots) ||
                (canonical_element_slots != 0U &&
                 canonical_field->element_count > SIZE_MAX / canonical_element_slots) ||
                !minic_c0_global_initializer_slot_count(
                    parser->program, field->type, &selected_element_slots) ||
                (selected_element_slots != 0U &&
                 field->element_count > SIZE_MAX / selected_element_slots)) {
                minic_parser_error(parser, "cannot resolve static union storage shape");
                return false;
            }
            canonical_slots = canonical_field->element_count * canonical_element_slots;
            selected_slots = field->element_count * selected_element_slots;
            if (canonical_slots != selected_slots) {
                minic_parser_error(parser,
                                   "backward static union member changes flattened storage shape");
                return false;
            }
            if (record_base_slot > SIZE_MAX - total) {
                return false;
            }
            slot_begin = record_base_slot + total;
            if (slot_begin > object->initializer_count ||
                canonical_slots > object->initializer_count - slot_begin) {
                minic_parser_error(parser, "backward static union storage is not materialized");
                return false;
            }
            slot_end = slot_begin + canonical_slots;
            for (slot = slot_begin; slot < slot_end; ++slot) {
                if (object->initializer_values[slot] != 0U) {
                    minic_parser_error(
                        parser,
                        "backward static union member can only replace implicit zero storage");
                    return false;
                }
            }
            for (relocation_index = 0U; relocation_index < object->relocation_count;
                 ++relocation_index) {
                const MinicGlobalRelocation *relocation;

                relocation = &object->relocations[relocation_index];
                if (relocation->location_kind ==
                        MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR &&
                    relocation->location_index >= slot_begin &&
                    relocation->location_index < slot_end) {
                    minic_parser_error(parser,
                                       "backward static union member cannot overwrite symbolic "
                                       "storage");
                    return false;
                }
            }
            union_record_id = (MinicRecordId)(current_record - parser->program->records);
            if (!minic_c0_global_object_select_union_member(parser->program,
                                                            object_id,
                                                            slot_begin,
                                                            union_record_id,
                                                            field_index) ||
                !overwrite_static_zero_field_value(parser, object_id, field, slot_begin)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(parser, "cannot replace static union active member");
                }
                return false;
            }
            return true;
        }
        {
            size_t relative;

            if (!minic_c0_global_record_field_initializer_slot(
                    parser->program, current_record, field_index, &relative) ||
                total > SIZE_MAX - relative) {
                return true;
            }
            total += relative;
        }
        if (depth + 1U == designator->depth) {
            return true;
        }
        if (!minic_type_is_record(field->type)) {
            return true;
        }
        current_record = minic_c0_program_record(parser->program, field->type.record_id);
        if (current_record == NULL || !current_record->is_complete) {
            return true;
        }
    }
    return true;
}'''
replace_between(
    parser,
    "static bool try_overwrite_static_zero_noncanonical_union_designator(",
    "static bool append_static_record_designator_value(",
    try_union,
)

append_designator = r'''static bool append_static_record_designator_value(MinicParser *parser,
                                                  MinicGlobalObjectId object_id,
                                                  const MinicRecord *record,
                                                  const size_t *field_indices,
                                                  size_t depth) {
    const MinicRecordField *field;
    size_t field_index;
    size_t field_limit;
    size_t selected_index;

    if (parser == NULL || record == NULL || !record->is_complete || field_indices == NULL ||
        depth == 0U) {
        return false;
    }
    field_limit = record->field_count;
    selected_index = field_indices[0];
    if (selected_index >= field_limit) {
        return false;
    }
    if (record->is_union) {
        MinicRecordId union_record_id;
        size_t union_base_slot;

        if (record < parser->program->records ||
            record >= parser->program->records + parser->program->record_count) {
            return false;
        }
        union_record_id = (MinicRecordId)(record - parser->program->records);
        union_base_slot = parser->program->global_objects[object_id].initializer_count;
        if (!minic_c0_global_object_select_union_member(
                parser->program, object_id, union_base_slot, union_record_id, selected_index)) {
            minic_parser_error(parser, "cannot record static union active member");
            return false;
        }
    } else {
        for (field_index = 0U; field_index < selected_index; ++field_index) {
            if (!append_static_field_zeros(parser, object_id, &record->fields[field_index])) {
                minic_parser_error(parser, "cannot zero-fill static record designator prefix");
                return false;
            }
        }
    }
    field = &record->fields[selected_index];
    if (field->element_count == 0U || field->is_flexible_array) {
        minic_parser_error(parser, "unsupported static record designator field");
        return false;
    }
    if (depth == 1U) {
        if (field->element_count == 1U && !field->is_array) {
            if (!minic_parser_parse_static_storage_initializer_value(
                    parser, object_id, field->type)) {
                return false;
            }
        } else if (field->is_array && minic_type_is_char_integer(field->type) &&
                   parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
            if (!minic_parser_add_bounded_string_literal_initializer(
                    parser, object_id, field->element_count)) {
                return false;
            }
        } else if (!parse_static_forward_array_initializer(
                       parser, object_id, field->type, field->element_count, false, NULL)) {
            return false;
        }
    } else {
        const MinicRecord *nested_record;

        if (field->element_count != 1U || field->is_array || field->is_bit_field ||
            !minic_type_is_record(field->type)) {
            minic_parser_error(
                parser, "nested static record designator path requires scalar record members");
            return false;
        }
        nested_record = minic_c0_program_record(parser->program, field->type.record_id);
        if (nested_record == NULL ||
            !append_static_record_designator_value(
                parser, object_id, nested_record, field_indices + 1U, depth - 1U)) {
            return false;
        }
    }
    if (!record->is_union) {
        for (field_index = selected_index + 1U; field_index < field_limit; ++field_index) {
            if (!append_static_field_zeros(parser, object_id, &record->fields[field_index])) {
                minic_parser_error(parser, "cannot zero-fill static record designator suffix");
                return false;
            }
        }
    }
    return true;
}'''
replace_between(
    parser,
    "static bool append_static_record_designator_value(",
    "static bool append_static_record_array_element_designator_value(",
    append_designator,
)

parse_union = r'''static bool parse_static_union_constant(MinicParser *parser,
                                        MinicGlobalObjectId object_id,
                                        const MinicRecord *record) {
    MinicStaticRecordDesignator designator;
    const MinicRecordField *field;
    MinicRecordId record_id;
    size_t selected_index;
    size_t union_base_slot;
    bool has_designator;

    if (parser == NULL || record == NULL || !record->is_complete || !record->is_union ||
        record->field_count == 0U || object_id >= parser->program->global_object_count ||
        record < parser->program->records ||
        record >= parser->program->records + parser->program->record_count ||
        !minic_parser_expect(parser, MINIC_TOKEN_LBRACE, "expected '{' in union initializer")) {
        return false;
    }
    record_id = (MinicRecordId)(record - parser->program->records);
    union_base_slot = parser->program->global_objects[object_id].initializer_count;
    if (parser->current.kind == MINIC_TOKEN_RBRACE) {
        if (!minic_c0_global_object_select_union_member(
                parser->program, object_id, union_base_slot, record_id, 0U) ||
            !append_static_field_zeros(parser, object_id, &record->fields[0])) {
            return false;
        }
        return minic_parser_advance(parser);
    }

    (void)memset(&designator, 0, sizeof(designator));
    has_designator = parser->current.kind == MINIC_TOKEN_DOT;
    if (has_designator) {
        if (!parse_static_record_designator_path(parser, record, &designator) ||
            designator.depth == 0U) {
            return false;
        }
        selected_index = designator.field_indices[0];
    } else {
        selected_index = 0U;
    }
    if (selected_index >= record->field_count ||
        !minic_c0_global_object_select_union_member(
            parser->program, object_id, union_base_slot, record_id, selected_index)) {
        minic_parser_error(parser, "cannot select static union initializer member");
        return false;
    }
    field = &record->fields[selected_index];
    if (field->element_count == 0U || field->is_flexible_array) {
        minic_parser_error(parser, "unsupported static union initializer member");
        return false;
    }

    if (has_designator && designator.has_array_index) {
        if (designator.depth != 1U || !field->is_array || field->is_bit_field ||
            designator.array_index >= field->element_count ||
            !append_static_record_array_element_designator_value(
                parser, object_id, field, designator.array_index)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot initialize designated static union array member");
            }
            return false;
        }
    } else if (has_designator && designator.depth > 1U) {
        const MinicRecord *nested_record;

        if (field->element_count != 1U || field->is_array || field->is_bit_field ||
            !minic_type_is_record(field->type)) {
            minic_parser_error(parser, "nested static union designator requires a record member");
            return false;
        }
        nested_record = minic_c0_program_record(parser->program, field->type.record_id);
        if (nested_record == NULL ||
            !append_static_record_designator_value(parser,
                                                   object_id,
                                                   nested_record,
                                                   designator.field_indices + 1U,
                                                   designator.depth - 1U)) {
            return false;
        }
    } else if (field->element_count == 1U && !field->is_array) {
        if (!minic_parser_parse_static_storage_initializer_value(parser, object_id, field->type)) {
            return false;
        }
    } else if (field->is_array && minic_type_is_char_integer(field->type) &&
               parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
        if (!minic_parser_add_bounded_string_literal_initializer(
                parser, object_id, field->element_count)) {
            return false;
        }
    } else if (!parse_static_forward_array_initializer(
                   parser, object_id, field->type, field->element_count, false, NULL)) {
        return false;
    }

    if (parser->current.kind == MINIC_TOKEN_COMMA) {
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
    if (parser->current.kind != MINIC_TOKEN_RBRACE) {
        minic_parser_error(parser, "union initializer may initialize only one active member");
        return false;
    }
    return minic_parser_advance(parser);
}'''
replace_between(
    parser,
    "static bool parse_static_union_constant(",
    "static bool parse_static_record_constant(",
    parse_union,
)

verifier = Path("src/frontend/ast_verifier.c")
replace_once(
    verifier,
    """            !storage_is_valid(
                object->relocations, object->relocation_count, object->relocation_capacity)) {
            return false;
        }
        if (object->relocation_count != 0U && !object->is_zero_initialized) {
""",
    """            !storage_is_valid(
                object->relocations, object->relocation_count, object->relocation_capacity) ||
            !storage_is_valid(object->union_selections,
                              object->union_selection_count,
                              object->union_selection_capacity) ||
            ((object->is_extern || object->is_tentative || object->is_zero_initialized) &&
             object->union_selection_count != 0U)) {
            return false;
        }
        {
            size_t selection_index;

            for (selection_index = 0U; selection_index < object->union_selection_count;
                 ++selection_index) {
                const MinicGlobalUnionSelection *selection;
                const MinicRecord *record;
                size_t prior_index;

                selection = &object->union_selections[selection_index];
                record = minic_c0_program_record(program, selection->record_id);
                if (record == NULL || !record->is_complete || !record->is_union ||
                    selection->field_index >= record->field_count ||
                    selection->initializer_slot > object->initializer_count) {
                    return false;
                }
                for (prior_index = 0U; prior_index < selection_index; ++prior_index) {
                    if (object->union_selections[prior_index].initializer_slot ==
                            selection->initializer_slot &&
                        object->union_selections[prior_index].record_id == selection->record_id) {
                        return false;
                    }
                }
            }
        }
        if (object->relocation_count != 0U && !object->is_zero_initialized) {
""",
)

codegen = Path("src/target/riscv64/codegen_function.c")
replace_once(
    codegen,
    """        size_t cursor;
        size_t field_index;
        size_t field_limit;
        size_t record_storage_size;

        record = minic_c0_program_record(program, type.record_id);
""",
    """        size_t cursor;
        size_t field_begin;
        size_t field_index;
        size_t field_limit;
        size_t record_base_slot;
        size_t record_storage_size;

        record_base_slot = *initializer_index;
        record = minic_c0_program_record(program, type.record_id);
""",
)
replace_once(
    codegen,
    """        cursor = 0U;
        field_limit = record->is_union ? 1U : record->field_count;
        for (field_index = 0U; field_index < field_limit; ++field_index) {
""",
    """        cursor = 0U;
        field_begin = 0U;
        field_limit = record->field_count;
        if (record->is_union) {
            size_t selected;

            selected = 0U;
            (void)minic_c0_global_object_union_member_selection(
                program, object, record_base_slot, type.record_id, &selected);
            if (selected >= record->field_count) {
                return false;
            }
            field_begin = selected;
            field_limit = selected + 1U;
        }
        for (field_index = field_begin; field_index < field_limit; ++field_index) {
""",
)

# Focused regression: forward promoted alternate members and backward union overlay.
test = Path("tests/compiler/c0/run-static-aggregate-initializers.sh")
replace_once(
    test,
    """test \"$(grep -c '  .dword 11' \"$build_dir/static_flexible_array.s\")\" -eq 3

if \"$minic\" -S \"$root/tests/compiler/c0/invalid_static_record_compound_literal_type.c\" \\
""",
    """test \"$(grep -c '  .dword 11' \"$build_dir/static_flexible_array.s\")\" -eq 3

cat >\"$build_dir/static_union_selection.c\" <<'EOF'
struct CallbackNode {
    int count;
    union {
        void (*func)(unsigned long);
        void (*callback)(struct CallbackNode *);
    };
    int tail;
};
static void callback_node(struct CallbackNode *node) { (void)node; }
static struct CallbackNode callback_holder = {
    .count = 1,
    .callback = callback_node,
    .tail = 2,
};

struct AnonymousStructUnion {
    union {
        struct { void *a0; void *a1; };
        struct { unsigned long s0; unsigned long s1; };
    };
};
static struct AnonymousStructUnion anonymous_struct_union = { .s1 = 8UL };

static long backward_target;
struct BackwardUnion {
    int prefix;
    union {
        int *as_int;
        long *as_long;
    };
    int tail;
};
static struct BackwardUnion backward_union = {
    .tail = 3,
    .as_long = &backward_target,
};
EOF
\"$minic\" -S \"$build_dir/static_union_selection.c\" \\
    -o \"$build_dir/static_union_selection.s\"
grep -F '  .dword callback_node' \"$build_dir/static_union_selection.s\" >/dev/null
grep -F '  .dword 8' \"$build_dir/static_union_selection.s\" >/dev/null
grep -F '  .dword backward_target' \"$build_dir/static_union_selection.s\" >/dev/null

if \"$minic\" -S \"$root/tests/compiler/c0/invalid_static_record_compound_literal_type.c\" \\
""",
)
replace_once(
    test,
    """static-fam=gnu-range+extended-storage designated-inner=shared mismatch=fail-closed'
""",
    """static-fam=gnu-range+extended-storage union-selection=forward+anonymous-struct+backward-reloc designated-inner=shared mismatch=fail-closed'
""",
)
