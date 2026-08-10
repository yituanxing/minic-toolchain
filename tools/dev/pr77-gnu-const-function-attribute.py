#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_function.c")
text = path.read_text()

old = '''           function_identifier_is(parser, "__pure__") ||
           function_identifier_is(parser, "__malloc__") ||
'''
new = '''           function_identifier_is(parser, "__pure__") ||
           function_attribute_name_is(parser, "const") ||
           function_attribute_name_is(parser, "__const__") ||
           function_identifier_is(parser, "__malloc__") ||
'''
if text.count(old) != 1:
    raise SystemExit(f"GNU const function attribute: expected one metadata anchor, found {text.count(old)}")
path.write_text(text.replace(old, new, 1))
print("staged GNU const function attribute as explicit non-ABI optimization metadata")
