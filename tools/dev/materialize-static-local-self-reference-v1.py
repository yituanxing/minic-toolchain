#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]
path = root / "src/frontend/parser_statement.c"
text = path.read_text()

old = '''    if (!minic_c0_program_add_global_object(parser->program,\n                                            symbol_name,\n                                            (size_t)symbol_length,\n                                            declared_type,\n                                            true,\n                                            minic_type_is_const(declared_type),\n                                            &object_id) ||\n        !minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '=' after static record") ||\n        !minic_parser_parse_static_storage_initializer_value(parser, object_id, declared_type) ||\n        !minic_parser_bind_scoped_global_object(parser, name_span, object_id)) {\n        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {\n            minic_parser_error(parser, "cannot initialize static local record storage");\n        }\n        return false;\n    }\n'''
new = '''    if (!minic_c0_program_add_global_object(parser->program,\n                                            symbol_name,\n                                            (size_t)symbol_length,\n                                            declared_type,\n                                            true,\n                                            minic_type_is_const(declared_type),\n                                            &object_id) ||\n        !minic_parser_bind_scoped_global_object(parser, name_span, object_id) ||\n        !minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '=' after static record") ||\n        !minic_parser_parse_static_storage_initializer_value(parser, object_id, declared_type)) {\n        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {\n            minic_parser_error(parser, "cannot initialize static local record storage");\n        }\n        return false;\n    }\n'''

if text.count(old) != 1:
    raise SystemExit(f"static record initializer anchor count={text.count(old)}")
path.write_text(text.replace(old, new, 1))
