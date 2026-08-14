#!/usr/bin/env python3
from pathlib import Path


path = Path("src/frontend/parser_record.c")
data = path.read_bytes()
needle = b"parser->diagnostic->message[0] == '\x00'"
replacement = b"parser->diagnostic->message[0] == '\\0'"
count = data.count(needle)
if count != 1:
    raise SystemExit(f"expected one generated NUL character literal, found {count}")
path.write_bytes(data.replace(needle, replacement, 1))
print("removed accidental NUL byte from staged parser_record.c")
