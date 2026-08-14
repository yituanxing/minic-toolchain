#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_expression.c")
text = path.read_text()
old = '''    case MINIC_TOKEN_KW_CONST:
    case MINIC_TOKEN_KW_CHAR:
    case MINIC_TOKEN_KW_INT:
    case MINIC_TOKEN_KW_LONG:
    case MINIC_TOKEN_KW_SIGNED:
    case MINIC_TOKEN_KW_UNSIGNED:
    case MINIC_TOKEN_KW_FLOAT:
    case MINIC_TOKEN_KW_DOUBLE:
    case MINIC_TOKEN_KW_VOID:
    case MINIC_TOKEN_KW_STRUCT:
        return true;
'''
new = '''    case MINIC_TOKEN_KW_CONST:
    case MINIC_TOKEN_KW_VOLATILE:
    case MINIC_TOKEN_KW_CHAR:
    case MINIC_TOKEN_KW_SHORT:
    case MINIC_TOKEN_KW_INT:
    case MINIC_TOKEN_KW_LONG:
    case MINIC_TOKEN_KW_SIGNED:
    case MINIC_TOKEN_KW_UNSIGNED:
    case MINIC_TOKEN_KW_FLOAT:
    case MINIC_TOKEN_KW_DOUBLE:
    case MINIC_TOKEN_KW_VOID:
    case MINIC_TOKEN_KW_STRUCT:
    case MINIC_TOKEN_KW_UNION:
        return true;
'''
if text.count(old) != 1:
    raise SystemExit(f"unexpected cast-type classifier count={text.count(old)}")
path.write_text(text.replace(old, new, 1))
print("staged short/union/volatile cast type classification")
