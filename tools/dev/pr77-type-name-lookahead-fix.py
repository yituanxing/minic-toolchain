#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_type.c")
text = path.read_text()
old = """    case MINIC_TOKEN_KW_BOOL:
    case MINIC_TOKEN_KW_CHAR:
    case MINIC_TOKEN_KW_INT:
    case MINIC_TOKEN_KW_LONG:
"""
new = """    case MINIC_TOKEN_KW_BOOL:
    case MINIC_TOKEN_KW_CHAR:
    case MINIC_TOKEN_KW_SHORT:
    case MINIC_TOKEN_KW_INT:
    case MINIC_TOKEN_KW_LONG:
"""
if text.count(old) != 1:
    raise SystemExit(f"short type-name lookahead anchor: expected one match, found {text.count(old)}")
text = text.replace(old, new, 1)
old = """    case MINIC_TOKEN_KW_STRUCT:
        return true;
"""
new = """    case MINIC_TOKEN_KW_STRUCT:
    case MINIC_TOKEN_KW_UNION:
    case MINIC_TOKEN_KW_ENUM:
        return true;
"""
if text.count(old) != 1:
    raise SystemExit(f"record type-name lookahead anchor: expected one match, found {text.count(old)}")
path.write_text(text.replace(old, new, 1))
print("restored short/union/enum cast and type-name lookahead after typeof unification")
