#!/usr/bin/env python3
from pathlib import Path

primary = Path("tools/dev/materialize-record-datalayout-query-v1.py")
source = primary.read_text(encoding="utf-8")
exec(compile(source, str(primary), "exec"), {"__name__": "__main__", "__file__": str(primary)})

path = Path("src/target/riscv64/codegen_expression.c")
text = path.read_text(encoding="utf-8")
include = '#include "target/data_layout.h"\n'
if include not in text:
    anchor = '#include "target/riscv64/codegen_internal.h"\n'
    if text.count(anchor) != 1:
        raise SystemExit("codegen_expression.c: include anchor changed")
    text = text.replace(anchor, anchor + include, 1)
path.write_text(text.rstrip() + "\n", encoding="utf-8")

print("NORMALIZED record-datalayout-query-v1")
