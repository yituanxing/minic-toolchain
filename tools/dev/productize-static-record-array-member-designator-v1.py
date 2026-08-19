#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count == 0 and new in text:
        return
    if count != 1:
        raise SystemExit(f"{label}: expected one replacement, found {count}")
    target.write_text(text.replace(old, new, 1))


replace_once(
    "src/frontend/parser_global.c",
    """typedef struct MinicStaticRecordDesignator {
    size_t field_indices[MINIC_GLOBAL_RELOCATION_MAX_MEMBER_DEPTH];
    size_t depth;
} MinicStaticRecordDesignator;
""",
    """typedef struct MinicStaticRecordDesignator {
    size_t field_indices[MINIC_GLOBAL_RELOCATION_MAX_MEMBER_DEPTH];
    size_t depth;
    bool has_array_index;
    size_t array_index;
} MinicStaticRecordDesignator;
""",
    "record-designator-shape",
)

replace_once(
    "src/frontend/parser_global.c",
    """        if (!minic_parser_advance(parser)) {
            return false;
        }
        if (parser->current.kind != MINIC_TOKEN_DOT) {
            break;
        }
        if (field == NULL || field->element_count != 1U || field->is_array || field->is_bit_field ||
            field->is_flexible_array || !minic_type_is_record(field->type)) {
            minic_parser_error(
                parser, \"intermediate static record designator member must be a scalar record\");
            return false;
        }
        current_record = minic_c0_program_record(parser->program, field->type.record_id);
        if (current_record == NULL || !current_record->is_complete) {
            minic_parser_error(parser,
                               \"static record designator path requires complete record members\");
            return false;
        }
    }
    return designator->depth != 0U &&
           minic_parser_expect(
               parser, MINIC_TOKEN_EQUAL, \"expected '=' after static record designator\");
""",
    """        if (!minic_parser_advance(parser)) {
            return false;
        }
        if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
            size_t first;
            size_t last;

            if (field == NULL || !field->is_array || field->element_count == 0U ||
                field->is_bit_field || field->is_flexible_array) {
                minic_parser_error(parser,
                                   \"array designator after record member requires a fixed array field\");
                return false;
            }
            if (!minic_parser_parse_array_designator(
                    parser, field->element_count, false, &first, &last)) {
                return false;
            }
            if (first != last) {
                minic_parser_error(
                    parser,
                    \"GNU range designators after record members are not supported yet\");
                return false;
            }
            designator->has_array_index = true;
            designator->array_index = first;
            return true;
        }
        if (parser->current.kind != MINIC_TOKEN_DOT) {
            break;
        }
        if (field == NULL || field->element_count != 1U || field->is_array || field->is_bit_field ||
            field->is_flexible_array || !minic_type_is_record(field->type)) {
            minic_parser_error(
                parser, \"intermediate static record designator member must be a scalar record\");
            return false;
        }
        current_record = minic_c0_program_record(parser->program, field->type.record_id);
        if (current_record == NULL || !current_record->is_complete) {
            minic_parser_error(parser,
                               \"static record designator path requires complete record members\");
            return false;
        }
    }
    return designator->depth != 0U &&
           minic_parser_expect(
               parser, MINIC_TOKEN_EQUAL, \"expected '=' after static record designator\");
""",
    "parse-record-array-terminal",
)

anchor = """static bool overwrite_static_zero_record_constant(MinicParser *parser,
                                                  MinicGlobalObjectId object_id,
                                                  const MinicRecord *record,
                                                  size_t record_base_slot);
"""
helper = """static bool append_static_record_array_element_designator_value(
    MinicParser *parser,
    MinicGlobalObjectId object_id,
    const MinicRecordField *field,
    size_t element_index) {
    size_t index;

    if (parser == NULL || field == NULL || !field->is_array || field->is_bit_field ||
        field->is_flexible_array || field->element_count == 0U ||
        element_index >= field->element_count) {
        return false;
    }
    for (index = 0U; index < element_index; ++index) {
        if (!append_static_constant_zero(parser, object_id, field->type)) {
            minic_parser_error(parser,
                               \"cannot zero-fill static record array designator prefix\");
            return false;
        }
    }
    if (!minic_parser_parse_static_storage_initializer_value(parser, object_id, field->type)) {
        return false;
    }
    for (index = element_index + 1U; index < field->element_count; ++index) {
        if (!append_static_constant_zero(parser, object_id, field->type)) {
            minic_parser_error(parser,
                               \"cannot zero-fill static record array designator suffix\");
            return false;
        }
    }
    return true;
}

"""
path = Path("src/frontend/parser_global.c")
text = path.read_text()
if helper not in text:
    if text.count(anchor) != 1:
        raise SystemExit("record-array-helper-anchor: expected one anchor")
    path.write_text(text.replace(anchor, helper + anchor, 1))

replace_once(
    "src/frontend/parser_global.c",
    """        overwrite_materialized_field = field_index < materialized_field_limit;
        if (has_designator && designator.depth > 1U) {
""",
    """        overwrite_materialized_field = field_index < materialized_field_limit;
        if (has_designator && designator.has_array_index) {
            size_t relative_slot;
            size_t field_base_slot;
            size_t element_base_slot;
            size_t element_slots;

            if (designator.depth != 1U || !field->is_array || field->is_bit_field ||
                field->is_flexible_array || designator.array_index >= field->element_count) {
                minic_parser_error(
                    parser,
                    \"static record array member designator currently requires a direct fixed array field\");
                return false;
            }
            if (overwrite_materialized_field) {
                if (!minic_c0_global_record_field_initializer_slot(
                        parser->program, record, field_index, &relative_slot) ||
                    !minic_c0_global_initializer_slot_count(
                        parser->program, field->type, &element_slots) ||
                    record_base_slot > SIZE_MAX - relative_slot) {
                    minic_parser_error(parser,
                                       \"cannot locate static record array member designator field\");
                    return false;
                }
                field_base_slot = record_base_slot + relative_slot;
                if (element_slots != 0U &&
                    designator.array_index > (SIZE_MAX - field_base_slot) / element_slots) {
                    minic_parser_error(parser,
                                       \"static record array member designator slot overflows\");
                    return false;
                }
                element_base_slot = field_base_slot + designator.array_index * element_slots;
                if (!overwrite_static_zero_array_element(
                        parser, object_id, field->type, element_base_slot)) {
                    return false;
                }
            } else {
                if (field_index != materialized_field_limit ||
                    !append_static_record_array_element_designator_value(
                        parser, object_id, field, designator.array_index)) {
                    if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
                        minic_parser_error(
                            parser, \"cannot materialize static record array member designator\");
                    }
                    return false;
                }
                materialized_field_limit += 1U;
            }
        } else if (has_designator && designator.depth > 1U) {
""",
    "materialize-record-array-terminal",
)

run_path = Path("tests/compiler/c0/run.sh")
run_text = run_path.read_text()
marker = "run-static-record-array-member-designator.sh"
if marker not in run_text:
    run_text += """

MINIC=\"$minic\" \\
HOST_CC=\"$host_cc\" \\
BUILD_DIR=\"${BUILD_DIR:-\"$root/build/debug\"}\" \\
sh \"$root/tests/compiler/c0/run-static-record-array-member-designator.sh\"
"""
    run_path.write_text(run_text)
