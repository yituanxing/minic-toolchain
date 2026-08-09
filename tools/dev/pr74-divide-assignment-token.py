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
    "src/frontend/token.h",
    """    MINIC_TOKEN_CARET_EQUAL,
    MINIC_TOKEN_SLASH,
    MINIC_TOKEN_PERCENT,
""",
    """    MINIC_TOKEN_CARET_EQUAL,
    MINIC_TOKEN_SLASH,
    MINIC_TOKEN_SLASH_EQUAL,
    MINIC_TOKEN_PERCENT,
""",
)
replace_once(
    "src/frontend/token.c",
    """    case MINIC_TOKEN_SLASH:
        return "/";
    case MINIC_TOKEN_PERCENT:
""",
    """    case MINIC_TOKEN_SLASH:
        return "/";
    case MINIC_TOKEN_SLASH_EQUAL:
        return "/=";
    case MINIC_TOKEN_PERCENT:
""",
)
replace_once(
    "src/frontend/lexer.c",
    """    case '/':
        token->kind = MINIC_TOKEN_SLASH;
        break;
""",
    """    case '/':
        if (minic_lexer_peek_next(lexer) == '=') {
            token->kind = MINIC_TOKEN_SLASH_EQUAL;
            minic_lexer_advance(lexer);
        } else {
            token->kind = MINIC_TOKEN_SLASH;
        }
        break;
""",
)
print("staged /= longest-match tokenization")
