#!/usr/bin/env python3
from pathlib import Path

path = Path("tests/compiler/c0/run-for-loops.sh")
text = path.read_text()
old = '    "prefix update requires a modifiable integer or pointer lvalue"\n'
new = '    "prefix update requires a modifiable scalar lvalue"\n'
if text.count(old) != 1:
    raise SystemExit(f"for-update diagnostic: expected one anchor, found {text.count(old)}")
path.write_text(text.replace(old, new, 1))
