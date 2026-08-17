#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]
path = root / "src/frontend/parser_statement.c"
text = path.read_text()

old = '''    (void)memset(&declaration_attributes, 0, sizeof(declaration_attributes));
    if (parser->current_function == MINIC_FUNCTION_INVALID ||
        !minic_parser_expect(parser, MINIC_TOKEN_KW_STATIC, "expected keyword 'static'") ||
        !minic_parser_parse_type_specifiers(parser, &base_type) ||
        !minic_parser_parse_gnu_attribute_lists(
            parser, consume_static_local_interleaved_attribute, &declaration_attributes)) {
        return false;
    }
'''
new = '''    (void)memset(&declaration_attributes, 0, sizeof(declaration_attributes));
    if (parser->current_function == MINIC_FUNCTION_INVALID ||
        !minic_parser_expect(parser, MINIC_TOKEN_KW_STATIC, "expected keyword 'static'") ||
        !minic_parser_parse_gnu_attribute_lists(
            parser, consume_static_local_interleaved_attribute, &declaration_attributes) ||
        !minic_parser_parse_type_specifiers(parser, &base_type) ||
        !minic_parser_parse_gnu_attribute_lists(
            parser, consume_static_local_interleaved_attribute, &declaration_attributes)) {
        return false;
    }
'''
if text.count(old) != 1:
    raise SystemExit(f"static-local declaration head count={text.count(old)}")
path.write_text(text.replace(old, new, 1))
