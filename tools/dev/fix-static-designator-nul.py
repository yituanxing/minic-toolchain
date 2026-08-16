#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_global.c")
data = path.read_bytes()
needle = b"'\x00'"
count = data.count(needle)
if count != 2:
    raise SystemExit(f"expected exactly 2 literal-NUL character constants, found {count}")
path.write_bytes(data.replace(needle, b"'\\0'"))
print("replaced 2 literal NUL bytes with canonical '\\0' source spelling")
