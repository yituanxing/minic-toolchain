#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_function.c")
text = path.read_text()

# format(...) is diagnostic/checking metadata; cold is an optimization hint.
# Neither changes the C type, calling convention, or object layout. Keep them in
# the explicit known-attribute set so unknown/ABI-affecting attributes remain
# rejected instead of being silently ignored.
old = '''           function_identifier_is(parser, "__always_inline__") ||
           function_identifier_is(parser, "noreturn") ||
'''
new = '''           function_identifier_is(parser, "__always_inline__") ||
           function_identifier_is(parser, "__format__") ||
           function_identifier_is(parser, "format") ||
           function_identifier_is(parser, "__cold__") ||
           function_identifier_is(parser, "cold") ||
           function_identifier_is(parser, "noreturn") ||
'''
if text.count(old) != 1:
    raise SystemExit(f"format/cold attribute whitelist: expected one anchor, found {text.count(old)}")
path.write_text(text.replace(old, new, 1))
print("staged GNU format diagnostic metadata and cold optimization hint attributes")
