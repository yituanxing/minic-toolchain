#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_type.c")
text = path.read_text()
old = '''    if (is_const && !minic_type_add_const(parsed_type, &parsed_type)) {
        minic_parser_error(parser, "cannot apply const qualifier");
        return false;
    }
    *type = parsed_type;
'''
new = '''    while (parser->current.kind == MINIC_TOKEN_KW_CONST) {
        is_const = true;
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
    if (is_const && !minic_type_add_const(parsed_type, &parsed_type)) {
        minic_parser_error(parser, "cannot apply const qualifier");
        return false;
    }
    *type = parsed_type;
'''
if text.count(old) != 1:
    raise SystemExit("unexpected type qualifier finalization")
path.write_text(text.replace(old, new, 1))
print("staged postfix const qualifiers in declaration specifiers")
