#!/usr/bin/env python3
from pathlib import Path


path = Path("src/frontend/lexer.c")
text = path.read_text()
old = """    if (length == 6U && memcmp(text, \"signed\", 6U) == 0) {
        return MINIC_TOKEN_KW_SIGNED;
    }
"""
new = """    if ((length == 6U && memcmp(text, \"signed\", 6U) == 0) ||
        (length == 8U && memcmp(text, \"__signed\", 8U) == 0) ||
        (length == 10U && memcmp(text, \"__signed__\", 10U) == 0)) {
        return MINIC_TOKEN_KW_SIGNED;
    }
"""
count = text.count(old)
if count != 1:
    raise SystemExit(f"expected one signed keyword classifier anchor, found {count}")
path.write_text(text.replace(old, new, 1))
print("staged GNU __signed/__signed__ as signed type-specifier aliases")
