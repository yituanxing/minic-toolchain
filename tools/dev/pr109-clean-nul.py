#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]
path = root / "src/frontend/parser_function.c"
data = path.read_bytes()
old = b"message[0] == '\x00'"
new = b"message[0] == '\\0'"
count = data.count(old)
if count != 1:
    raise SystemExit(f"expected one embedded-NUL literal, found {count}")
path.write_bytes(data.replace(old, new, 1))
