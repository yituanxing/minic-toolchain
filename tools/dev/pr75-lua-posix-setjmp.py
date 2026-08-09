#!/usr/bin/env python3
from pathlib import Path

path = Path("tests/external/lua/probe.sh")
text = path.read_text()
old = '''int setjmp(jmp_buf environment);\nvoid longjmp(jmp_buf environment, int value);\n'''
new = '''int setjmp(jmp_buf environment);\nvoid longjmp(jmp_buf environment, int value);\nint _setjmp(jmp_buf environment);\nvoid _longjmp(jmp_buf environment, int value);\n'''
count = text.count(old)
if count != 1:
    raise SystemExit(f"Lua controlled setjmp surface: expected 1 match, found {count}")
path.write_text(text.replace(old, new, 1))
print("staged Lua POSIX _setjmp/_longjmp declarations")
