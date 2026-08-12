#!/usr/bin/env python3
from pathlib import Path
import re

p = Path("src/frontend/parser_typedef.c")
data = p.read_bytes()
if b"\x00" in data:
    data = data.replace(b"\x00", b"\\0")
p.write_bytes(data)

text = p.read_text()
text, count = re.subn(
    r'''static bool typedef_token_text_equals\(const MinicParser \*parser, const char \*text\) \{.*?\n}\n\n''',
    "",
    text,
    count=1,
    flags=re.S,
)
if count != 1:
    raise SystemExit(f"unused typedef token helper removal count={count}")
p.write_text(text)
