#!/usr/bin/env python3
"""Materialize promoted anonymous-member paths in static record designators."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PATH = ROOT / "src/frontend/parser_global.c"

old = '''        field_span = parser->current.span;
        if (!minic_parser_find_record_field_path(parser, current_record, field_span, &field_path) ||
            !field_path.found || field_path.ambiguous || field_path.depth != 1U) {
            minic_parser_error(parser,
                               "static record designator requires a direct unambiguous member at "
                               "each path segment");
            return false;
        }
        field_index = field_path.field_indices[0];
        designator->field_indices[designator->depth++] = field_index;
        field = minic_c0_record_field(current_record, field_index);
        if (field == NULL || !minic_parser_advance(parser)) {
            return false;
        }
        if (parser->current.kind != MINIC_TOKEN_DOT) {
            break;
        }
        if (field->element_count != 1U || field->is_array || field->is_bit_field ||
            field->is_flexible_array || !minic_type_is_record(field->type)) {
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
'''

new = '''        field_span = parser->current.span;
        if (!minic_parser_find_record_field_path(parser, current_record, field_span, &field_path) ||
            !field_path.found || field_path.ambiguous) {
            minic_parser_error(parser,
                               "static record designator requires an unambiguous member path");
            return false;
        }
        if (field_path.depth == 0U ||
            field_path.depth > MINIC_GLOBAL_RELOCATION_MAX_MEMBER_DEPTH - designator->depth) {
            minic_parser_error(parser,
                               "static record designator path exceeds implementation limit");
            return false;
        }
        field = NULL;
        for (size_t path_index = 0U; path_index < field_path.depth; ++path_index) {
            const MinicRecord *path_record;

            path_record = minic_c0_program_record(parser->program, field_path.record_ids[path_index]);
            field_index = field_path.field_indices[path_index];
            if (path_record == NULL || path_record != current_record) {
                minic_parser_error(parser, "invalid promoted static record designator path");
                return false;
            }
            field = minic_c0_record_field(path_record, field_index);
            if (field == NULL) {
                return false;
            }
            designator->field_indices[designator->depth++] = field_index;
            if (path_index + 1U != field_path.depth) {
                if (field->element_count != 1U || field->is_array || field->is_bit_field ||
                    field->is_flexible_array || !field->is_anonymous_member ||
                    !minic_type_is_record(field->type)) {
                    minic_parser_error(
                        parser, "promoted static designator path must traverse anonymous records");
                    return false;
                }
                current_record = minic_c0_program_record(parser->program, field->type.record_id);
                if (current_record == NULL || !current_record->is_complete) {
                    minic_parser_error(
                        parser, "static record designator path requires complete record members");
                    return false;
                }
            }
        }
        if (field == NULL || !minic_parser_advance(parser)) {
            return false;
        }
        if (parser->current.kind != MINIC_TOKEN_DOT) {
            break;
        }
        if (field->element_count != 1U || field->is_array || field->is_bit_field ||
            field->is_flexible_array || !minic_type_is_record(field->type)) {
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
'''

text = PATH.read_text()
if new in text:
    print("promoted static record designator product already materialized")
    raise SystemExit(0)
if text.count(old) != 1:
    raise SystemExit(f"static record designator anchor changed: matches={text.count(old)}")
PATH.write_text(text.replace(old, new, 1))
print("materialized promoted anonymous-member static record designator paths")
