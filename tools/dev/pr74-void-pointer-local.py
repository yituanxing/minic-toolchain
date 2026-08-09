#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_statement.c")
text = path.read_text()

function_start = text.index("static bool parse_local_declarator(MinicParser *parser, MinicType base_type) {")
function_end = text.index("\nstatic bool parse_declaration(MinicParser *parser) {", function_start)
function_text = text[function_start:function_end]

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
if function_text.count(old) != 1:
    raise SystemExit("unexpected local declarator type block")
function_text = function_text.replace(old, new, 1)
text = text[:function_start] + function_text + text[function_end:]

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
