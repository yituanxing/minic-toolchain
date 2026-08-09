#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_statement.c")
text = path.read_text()

old = '''    if (!minic_parser_parse_pointer_declarator(parser, base_type, &declared_type)) {
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
'''
new = '''    if (!minic_parser_parse_pointer_declarator(parser, base_type, &declared_type)) {
        return false;
    }
    if (minic_type_is_void(declared_type)) {
        minic_parser_error(parser, "local object cannot have void type");
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
'''
if text.count(old) != 1:
    raise SystemExit("unexpected local declarator type block")
text = text.replace(old, new, 1)

old = '''    if (!minic_parser_parse_type_specifiers(parser, &base_type)) {
        return false;
    }
    if (minic_type_is_void(base_type)) {
        minic_parser_error(parser, "local object cannot have void type");
        return false;
    }

    for (;;) {
'''
new = '''    if (!minic_parser_parse_type_specifiers(parser, &base_type)) {
        return false;
    }

    for (;;) {
'''
if text.count(old) != 1:
    raise SystemExit("unexpected declaration bare-void guard")
path.write_text(text.replace(old, new, 1))
print("staged local bare-void validation after pointer declarators")
