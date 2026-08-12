#!/usr/bin/env python3
from pathlib import Path
import re

p = Path("src/frontend/parser_statement.c")
text = p.read_text()
pattern = re.compile(
    r'''    \{\n        bool inferred_array;\n\n        inferred_array = false;\n        if \(parser->current.kind == MINIC_TOKEN_LBRACKET\) \{\n(?P<bracket>.*?)            local.is_array = true;\n        \}\n        if \(!parse_local_object_attributes\(parser, &attributes\)\) \{\n(?P<attrs>.*?)        if \(parser->current.kind == MINIC_TOKEN_EQUAL && local.is_array\) \{\n(?P<init>.*?)        \}\n    \}\n    if \(!local.is_array\) \{''',
    re.S,
)
match = pattern.search(text)
if match is None:
    raise SystemExit("array registration block anchor not found")
replacement = '''    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {\n        bool inferred_array;\n\n        inferred_array = false;\n''' + match.group("bracket") + '''        local.is_array = true;\n        if (!parse_local_object_attributes(parser, &attributes)) {\n''' + match.group("attrs") + '''        if (parser->current.kind == MINIC_TOKEN_EQUAL) {\n''' + match.group("init") + '''        }\n    }\n    if (!local.is_array) {'''
updated, count = pattern.subn(replacement, text, count=1)
if count != 1:
    raise SystemExit(f"array registration block replacement count={count}")
p.write_text(updated)
