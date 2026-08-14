#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_core.c")
text = path.read_text()

start = text.find("static bool array_bound_parenthesis_starts_integer_cast(")
end = text.find("\nstatic bool array_bound_apply_integer_cast(", start)
if start < 0 or end < 0:
    raise SystemExit("cannot locate array-bound cast lookahead")
body = text[start:end]
old = """    case MINIC_TOKEN_KW_VOLATILE:
    case MINIC_TOKEN_KW_CHAR:
"""
new = """    case MINIC_TOKEN_KW_VOLATILE:
    case MINIC_TOKEN_KW_BOOL:
    case MINIC_TOKEN_KW_CHAR:
"""
if body.count(old) != 1:
    raise SystemExit(f"expected one _Bool cast-lookahead anchor, found {body.count(old)}")
text = text[:start] + body.replace(old, new, 1) + text[end:]

start = text.find("static bool array_bound_apply_integer_cast(")
end = text.find("\nstatic bool parse_array_bound_cast(", start)
if start < 0 or end < 0:
    raise SystemExit("cannot locate array-bound integer cast evaluator")
body = text[start:end]
old = """    switch (type.integer_rank) {
    case MINIC_INTEGER_RANK_CHAR:
"""
new = """    switch (type.integer_rank) {
    case MINIC_INTEGER_RANK_BOOL:
        *value = operand != 0 ? 1 : 0;
        return true;
    case MINIC_INTEGER_RANK_CHAR:
"""
if body.count(old) != 1:
    raise SystemExit(f"expected one _Bool cast evaluator anchor, found {body.count(old)}")
text = text[:start] + body.replace(old, new, 1) + text[end:]
path.write_text(text)
print("staged _Bool constant-expression casts as nonzero-to-one rather than one-bit truncation")
