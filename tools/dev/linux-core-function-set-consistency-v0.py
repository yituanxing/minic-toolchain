#!/usr/bin/env python3
from pathlib import Path

p = Path("src/compiler/compiler.c")
text = p.read_text()
old = "if (function->is_internal && function->is_inline && !function->is_referenced)"
new = "if (function->is_internal && !function->is_referenced)"
count = text.count(old)
if count != 1:
    raise SystemExit(f"compiler.c: expected one remaining Core emission gate, found {count}")
p.write_text(text.replace(old, new, 1))
print("LINUX_CORE_FUNCTION_SET_CONSISTENCY_PATCH=APPLIED")
