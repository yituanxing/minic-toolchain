#!/usr/bin/env python3
from pathlib import Path

for path_value in (
    "tests/compiler/c0/run-for-loops.sh",
    "tests/compiler/c0/run-const-locals.sh",
):
    path = Path(path_value)
    text = path.read_text()
    old = '    "prefix update requires a modifiable integer or pointer lvalue"\n'
    new = '    "prefix update requires a modifiable scalar lvalue"\n'
    if text.count(old) != 1:
        raise SystemExit(
            f"generic update diagnostic {path_value}: expected one anchor, found {text.count(old)}"
        )
    path.write_text(text.replace(old, new, 1))
