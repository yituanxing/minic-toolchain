#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]
path = root / "src/frontend/parser_global.c"
text = path.read_text()

anchor = '''static bool append_static_record_designator_value(MinicParser *parser,\n'''
helper = r'''static bool try_overwrite_static_zero_noncanonical_union_designator(
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
        if (field == NULL || field->element_count != 1U || field->is_array ||
            field->is_bit_field || field->is_flexible_array) {
            return true;
        }
        if (current_record->is_union && field_index != 0U) {
            const MinicRecordField *canonical_field;
            size_t canonical_element_slots;
            size_t canonical_slots;
            size_t slot_begin;
            size_t slot_end;
            size_t slot;
            size_t relocation_index;
            uint64_t bits;

            *handled = true;
            if (depth + 1U != designator->depth || !minic_type_is_integer(field->type) ||
                current_record->field_count == 0U) {
                minic_parser_error(
                    parser,
                    "backward noncanonical static union designator requires an integer scalar leaf");
                return false;
            }
            canonical_field = &current_record->fields[0];
            if (canonical_field->element_count == 0U || canonical_field->is_flexible_array ||
                !minic_c0_global_initializer_slot_count(
                    parser->program, canonical_field->type, &canonical_element_slots) ||
                (canonical_element_slots != 0U &&
                 canonical_field->element_count > SIZE_MAX / canonical_element_slots)) {
                minic_parser_error(parser, "cannot resolve canonical static union storage");
                return false;
            }
            canonical_slots = canonical_field->element_count * canonical_element_slots;
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
                        "backward noncanonical static union zero requires zero canonical storage");
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
                    minic_parser_error(
                        parser,
                        "backward noncanonical static union zero cannot overwrite symbolic storage");
                    return false;
                }
            }
            if (!minic_parser_parse_integer_initializer_bits(parser, field->type, &bits) ||
                bits != 0U) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(
                        parser,
                        "backward noncanonical static union member requires a zero initializer");
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
}

'''
if text.count(anchor) != 1:
    raise SystemExit(f"helper insertion anchor count={text.count(anchor)}")
text = text.replace(anchor, helper + anchor, 1)

old = '''            if (overwrite_materialized_field) {\n                size_t relative_slot;\n                size_t slot_index;\n                MinicType slot_type;\n\n                if (!static_record_designator_scalar_slot(\n                        parser->program, record, &designator, &relative_slot, &slot_type) ||\n                    record_base_slot > SIZE_MAX - relative_slot) {\n                    minic_parser_error(parser,\n                                       "backward nested static record designator currently "\n                                       "requires a scalar leaf");\n                    return false;\n                }\n                slot_index = record_base_slot + relative_slot;\n                if (!parse_static_scalar_constant_at(\n                        parser, object_id, slot_type, true, slot_index)) {\n                    return false;\n                }\n'''
new = '''            if (overwrite_materialized_field) {\n                size_t relative_slot;\n                size_t slot_index;\n                MinicType slot_type;\n                bool handled_union_zero;\n\n                if (!try_overwrite_static_zero_noncanonical_union_designator(\n                        parser,\n                        object_id,\n                        record,\n                        &designator,\n                        record_base_slot,\n                        &handled_union_zero)) {\n                    return false;\n                }\n                if (handled_union_zero) {\n                    /* The selected noncanonical union member denotes the same\n                     * already-materialized all-zero bytes. No flattened scalar\n                     * rewrite is needed or representable. */\n                } else {\n                    if (!static_record_designator_scalar_slot(\n                            parser->program, record, &designator, &relative_slot, &slot_type) ||\n                        record_base_slot > SIZE_MAX - relative_slot) {\n                        minic_parser_error(parser,\n                                           "backward nested static record designator currently "\n                                           "requires a scalar leaf");\n                        return false;\n                    }\n                    slot_index = record_base_slot + relative_slot;\n                    if (!parse_static_scalar_constant_at(\n                            parser, object_id, slot_type, true, slot_index)) {\n                        return false;\n                    }\n                }\n'''
if text.count(old) != 1:
    raise SystemExit(f"backward nested designator anchor count={text.count(old)}")
text = text.replace(old, new, 1)

path.write_text(text)
