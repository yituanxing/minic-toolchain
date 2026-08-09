#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_statement.c")
text = path.read_text()
old = '''    case MINIC_TOKEN_KW_VOID:
    case MINIC_TOKEN_KW_STRUCT:
        return true;
'''
new = '''    case MINIC_TOKEN_KW_VOID:
    case MINIC_TOKEN_KW_STRUCT:
    case MINIC_TOKEN_KW_UNION:
        return true;
'''
if text.count(old) != 1:
    raise SystemExit(f"unexpected local declaration classifier count={text.count(old)}")
path.write_text(text.replace(old, new, 1))
print("staged union local declaration classification")
