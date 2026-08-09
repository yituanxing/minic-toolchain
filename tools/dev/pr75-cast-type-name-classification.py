#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}: {old[:120]!r}")
    target.write_text(text.replace(old, new, 1))


replace_once(
    "src/frontend/parser_expression.c",
    """    case MINIC_TOKEN_KW_CONST:
    case MINIC_TOKEN_KW_CHAR:
""",
    """    case MINIC_TOKEN_KW_CONST:
    case MINIC_TOKEN_KW_VOLATILE:
    case MINIC_TOKEN_KW_CHAR:
""",
)

replace_once(
    "src/frontend/parser_expression.c",
    """    case MINIC_TOKEN_KW_VOID:
    case MINIC_TOKEN_KW_STRUCT:
        return true;
""",
    """    case MINIC_TOKEN_KW_VOID:
    case MINIC_TOKEN_KW_STRUCT:
    case MINIC_TOKEN_KW_UNION:
        return true;
""",
)

print("staged cast lookahead for volatile and union type names")
