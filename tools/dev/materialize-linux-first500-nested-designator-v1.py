#!/usr/bin/env python3
"""Generalize backward nested static record designators to aggregate leaves once."""
from pathlib import Path


def replace_once(path: Path, before: str, after: str) -> None:
    text = path.read_text()
    if after in text:
        return
    count = text.count(before)
    if count != 1:
        raise SystemExit(f"{path}: expected one materialization anchor, found {count}")
    path.write_text(text.replace(before, after, 1))


global_parser = Path("src/frontend/parser_global.c")
replace_once(
    global_parser,
    """static bool static_record_designator_scalar_slot(const MinicC0Program *program,
                                                 const MinicRecord *record,
                                                 const MinicStaticRecordDesignator *designator,
                                                 size_t *slot_index,
                                                 MinicType *slot_type) {
    const MinicRecord *current_record;
    size_t depth;
    size_t total;

    if (program == NULL || record == NULL || designator == NULL || slot_index == NULL ||
        slot_type == NULL || designator->depth == 0U) {
        return false;
    }
    current_record = record;
    total = 0U;
    for (depth = 0U; depth < designator->depth; ++depth) {
        const MinicRecordField *field;
        size_t relative;
        size_t field_index;

        field_index = designator->field_indices[depth];
        field = minic_c0_record_field(current_record, field_index);
        if (field == NULL || field->element_count != 1U || field->is_array || field->is_bit_field ||
            field->is_flexible_array ||
            !minic_c0_global_record_field_initializer_slot(
                program, current_record, field_index, &relative) ||
            total > SIZE_MAX - relative) {
            return false;
        }
        total += relative;
        if (depth + 1U == designator->depth) {
            if (!minic_type_is_integer(field->type) && !minic_type_is_pointer(field->type)) {
                return false;
            }
            *slot_index = total;
            *slot_type = field->type;
            return true;
        }
        if (!minic_type_is_record(field->type)) {
            return false;
        }
        current_record = minic_c0_program_record(program, field->type.record_id);
        if (current_record == NULL || !current_record->is_complete) {
            return false;
        }
    }
    return false;
}
""",
    """static bool static_record_designator_leaf_slot(const MinicC0Program *program,
                                               const MinicRecord *record,
                                               const MinicStaticRecordDesignator *designator,
                                               size_t *slot_index,
                                               const MinicRecordField **leaf_field) {
    const MinicRecord *current_record;
    size_t depth;
    size_t total;

    if (program == NULL || record == NULL || designator == NULL || slot_index == NULL ||
        leaf_field == NULL || designator->depth == 0U) {
        return false;
    }
    current_record = record;
    total = 0U;
    for (depth = 0U; depth < designator->depth; ++depth) {
        const MinicRecordField *field;
        size_t relative;
        size_t field_index;

        field_index = designator->field_indices[depth];
        field = minic_c0_record_field(current_record, field_index);
        if (field == NULL || field->element_count == 0U || field->is_flexible_array ||
            !minic_c0_global_record_field_initializer_slot(
                program, current_record, field_index, &relative) ||
            total > SIZE_MAX - relative) {
            return false;
        }
        total += relative;
        if (depth + 1U == designator->depth) {
            *slot_index = total;
            *leaf_field = field;
            return true;
        }
        if (field->element_count != 1U || field->is_array || field->is_bit_field ||
            !minic_type_is_record(field->type)) {
            return false;
        }
        current_record = minic_c0_program_record(program, field->type.record_id);
        if (current_record == NULL || !current_record->is_complete) {
            return false;
        }
    }
    return false;
}
""",
)

replace_once(
    global_parser,
    """                size_t relative_slot;
                size_t slot_index;
                MinicType slot_type;
                bool handled_union_zero;
""",
    """                size_t relative_slot;
                size_t slot_index;
                const MinicRecordField *leaf_field;
                bool handled_union_zero;
""",
)

replace_once(
    global_parser,
    """                    if (!static_record_designator_scalar_slot(
                            parser->program, record, &designator, &relative_slot, &slot_type) ||
                        record_base_slot > SIZE_MAX - relative_slot) {
                        minic_parser_error(parser,
                                           \"backward nested static record designator currently \"
                                           \"requires a scalar leaf\");
                        return false;
                    }
                    slot_index = record_base_slot + relative_slot;
                    if (!parse_static_scalar_constant_at(
                            parser, object_id, slot_type, true, slot_index)) {
                        return false;
                    }
""",
    """                    if (!static_record_designator_leaf_slot(
                            parser->program, record, &designator, &relative_slot, &leaf_field) ||
                        record_base_slot > SIZE_MAX - relative_slot) {
                        minic_parser_error(parser,
                                           \"cannot locate backward nested static record \"
                                           \"designator leaf\");
                        return false;
                    }
                    slot_index = record_base_slot + relative_slot;
                    if (!overwrite_static_zero_field_value(
                            parser, object_id, leaf_field, slot_index)) {
                        return false;
                    }
""",
)
