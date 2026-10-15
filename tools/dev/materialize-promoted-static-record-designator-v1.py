#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]
path = root / "src/frontend/parser_global.c"
text = path.read_text()
start_marker = "static bool parse_static_record_designator_path(MinicParser *parser,\n"
end_marker = "\nstatic bool static_record_designator_scalar_slot("
start = text.find(start_marker)
end = text.find(end_marker, start)
if start < 0 or end < 0:
    raise SystemExit("static record designator parser anchors changed")

replacement = r'''static bool parse_static_record_designator_path(MinicParser *parser,
                                                const MinicRecord *record,
                                                MinicStaticRecordDesignator *designator) {
    const MinicRecord *current_record;

    if (parser == NULL || record == NULL || designator == NULL ||
        parser->current.kind != MINIC_TOKEN_DOT) {
        return false;
    }
    (void)memset(designator, 0, sizeof(*designator));
    current_record = record;
    while (parser->current.kind == MINIC_TOKEN_DOT) {
        MinicRecordFieldPath field_path;
        MinicSourceSpan field_span;
        const MinicRecordField *field;
        size_t path_index;

        if (!minic_parser_advance(parser) || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
            minic_parser_error(parser, "expected member name after '.' in initializer");
            return false;
        }
        field_span = parser->current.span;
        if (!minic_parser_find_record_field_path(parser, current_record, field_span, &field_path) ||
            !field_path.found || field_path.ambiguous || field_path.depth == 0U) {
            minic_parser_error(parser,
                               "static record designator requires an unambiguous member path");
            return false;
        }
        if (field_path.depth >
            MINIC_GLOBAL_RELOCATION_MAX_MEMBER_DEPTH - designator->depth) {
            minic_parser_error(parser,
                               "static record designator path exceeds implementation limit");
            return false;
        }
        field = NULL;
        for (path_index = 0U; path_index < field_path.depth; ++path_index) {
            size_t field_index;

            field_index = field_path.field_indices[path_index];
            designator->field_indices[designator->depth++] = field_index;
            field = minic_c0_record_field(current_record, field_index);
            if (field == NULL) {
                return false;
            }
            if (path_index + 1U < field_path.depth) {
                if (field->element_count != 1U || field->is_array || field->is_bit_field ||
                    field->is_flexible_array || !minic_type_is_record(field->type)) {
                    minic_parser_error(
                        parser,
                        "promoted static record designator path requires scalar record members");
                    return false;
                }
                current_record =
                    minic_c0_program_record(parser->program, field->type.record_id);
                if (current_record == NULL || !current_record->is_complete) {
                    minic_parser_error(
                        parser, "static record designator path requires complete record members");
                    return false;
                }
            }
        }
        if (!minic_parser_advance(parser)) {
            return false;
        }
        if (parser->current.kind != MINIC_TOKEN_DOT) {
            break;
        }
        if (field == NULL || field->element_count != 1U || field->is_array ||
            field->is_bit_field || field->is_flexible_array ||
            !minic_type_is_record(field->type)) {
            minic_parser_error(
                parser, "intermediate static record designator member must be a scalar record");
            return false;
        }
        current_record = minic_c0_program_record(parser->program, field->type.record_id);
        if (current_record == NULL || !current_record->is_complete) {
            minic_parser_error(parser,
                               "static record designator path requires complete record members");
            return false;
        }
    }
    return designator->depth != 0U &&
           minic_parser_expect(
               parser, MINIC_TOKEN_EQUAL, "expected '=' after static record designator");
}
'''

text = text[:start] + replacement + text[end:]
path.write_text(text)
print("materialized promoted static record designator paths")
