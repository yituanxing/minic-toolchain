#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]
path = root / "src/frontend/parser_global.c"
text = path.read_text()

old_decl = '''static bool overwrite_static_zero_record_constant(MinicParser *parser,\n                                                  MinicGlobalObjectId object_id,\n                                                  const MinicRecord *record,\n                                                  size_t record_base_slot);\n\nstatic bool overwrite_static_zero_field_value(MinicParser *parser,\n'''
new_decl = '''static bool overwrite_static_zero_record_constant(MinicParser *parser,\n                                                  MinicGlobalObjectId object_id,\n                                                  const MinicRecord *record,\n                                                  size_t record_base_slot);\n\nstatic bool overwrite_static_zero_record_value(MinicParser *parser,\n                                               MinicGlobalObjectId object_id,\n                                               MinicType type,\n                                               const MinicRecord *record,\n                                               size_t record_base_slot);\n\nstatic bool overwrite_static_zero_field_value(MinicParser *parser,\n'''

old_record = '''        if (minic_type_is_record(field->type)) {\n            const MinicRecord *nested_record;\n\n            nested_record = minic_c0_program_record(parser->program, field->type.record_id);\n            return nested_record != NULL && overwrite_static_zero_record_constant(\n                                                parser, object_id, nested_record, field_base_slot);\n        }\n'''
new_record = '''        if (minic_type_is_record(field->type)) {\n            const MinicRecord *nested_record;\n\n            nested_record = minic_c0_program_record(parser->program, field->type.record_id);\n            return nested_record != NULL &&\n                   overwrite_static_zero_record_value(\n                       parser, object_id, field->type, nested_record, field_base_slot);\n        }\n'''

anchor = '''static bool overwrite_static_zero_record_constant(MinicParser *parser,\n                                                  MinicGlobalObjectId object_id,\n                                                  const MinicRecord *record,\n                                                  size_t record_base_slot) {\n'''
helper = '''static bool overwrite_static_zero_record_value(MinicParser *parser,\n                                               MinicGlobalObjectId object_id,\n                                               MinicType type,\n                                               const MinicRecord *record,\n                                               size_t record_base_slot) {\n    if (parser == NULL || record == NULL || !record->is_complete ||\n        !minic_type_is_record(type)) {\n        return false;\n    }\n    if (parser->current.kind == MINIC_TOKEN_LPAREN) {\n        MinicParser probe;\n        MinicType explicit_type;\n\n        probe = *parser;\n        if (!minic_parser_advance(&probe)) {\n            return false;\n        }\n        if (probe.current.kind == MINIC_TOKEN_LPAREN) {\n            if (!minic_parser_advance(parser) ||\n                !overwrite_static_zero_record_value(\n                    parser, object_id, type, record, record_base_slot) ||\n                !minic_parser_expect(parser,\n                                     MINIC_TOKEN_RPAREN,\n                                     \"expected ')' after grouped static record initializer\")) {\n                return false;\n            }\n            return true;\n        }\n        if (!minic_parser_advance(parser) ||\n            !minic_parser_parse_type_name(parser, &explicit_type) ||\n            !minic_parser_expect(parser,\n                                 MINIC_TOKEN_RPAREN,\n                                 \"expected ')' after static compound literal type\")) {\n            return false;\n        }\n        if (!minic_type_is_record(explicit_type) ||\n            !minic_type_assignment_compatible(type, explicit_type)) {\n            minic_parser_error(parser, \"static record compound literal type mismatch\");\n            return false;\n        }\n        if (parser->current.kind != MINIC_TOKEN_LBRACE) {\n            minic_parser_error(parser,\n                               \"static record compound literal requires initializer list\");\n            return false;\n        }\n    }\n    return overwrite_static_zero_record_constant(\n        parser, object_id, record, record_base_slot);\n}\n\n'''

for old, new, label in [
    (old_decl, new_decl, "forward declarations"),
    (old_record, new_record, "record field overwrite"),
]:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label} anchor count={count}")
    text = text.replace(old, new, 1)

if text.count(anchor) != 1:
    raise SystemExit(f"record constant anchor count={text.count(anchor)}")
text = text.replace(anchor, helper + anchor, 1)
path.write_text(text)
