#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_expression.c")
text = path.read_text()
old = "    expression.span.end = parser->previous.span.end;\n"
new = "    expression.span.end = result_pointer->span.end;\n"
if text.count(old) != 1:
    raise SystemExit(f"overflow builtin span fix: expected one generated previous-token use, found {text.count(old)}")
path.write_text(text.replace(old, new, 1))
print("fixed overflow builtin source span without requiring parser previous-token state")
