#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_core.c")
text = path.read_text()
old = '''    if (parser->current.kind == MINIC_TOKEN_INTEGER_CONSTANT) {
        return minic_parser_parse_integer_value64(parser, value);
    }
    if (parser->current.kind == MINIC_TOKEN_KW_SIZEOF) {
        return parse_array_bound_sizeof(parser, value);
    }
'''
new = '''    if (parser->current.kind == MINIC_TOKEN_INTEGER_CONSTANT) {
        return minic_parser_parse_integer_value64(parser, value);
    }
    if (parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
        int enum_value;

        if (minic_parser_find_enum_constant(parser, parser->current.span, &enum_value)) {
            *value = (int64_t)enum_value;
            return minic_parser_advance(parser);
        }
    }
    if (parser->current.kind == MINIC_TOKEN_KW_SIZEOF) {
        return parse_array_bound_sizeof(parser, value);
    }
'''
if text.count(old) != 1:
    raise SystemExit(f"unexpected array-bound primary constant block count={text.count(old)}")
path.write_text(text.replace(old, new, 1))
print("staged enum constants in fixed array bounds")
