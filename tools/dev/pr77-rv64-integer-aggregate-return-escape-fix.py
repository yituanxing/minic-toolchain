#!/usr/bin/env python3
from pathlib import Path

path = Path("src/target/riscv64/codegen_statement.c")
text = path.read_text()
old = '''fprintf(file, "  mv t0, a0
  ld a0, 0(t0)
")'''
new = 'fprintf(file, "  mv t0, a0\\n  ld a0, 0(t0)\\n")'
if text.count(old) != 1:
    raise SystemExit(f"aggregate return first-chunk escape: expected one broken string, found {text.count(old)}")
text = text.replace(old, new, 1)
old = '''fprintf(file, "  ld a1, 8(t0)
")'''
new = 'fprintf(file, "  ld a1, 8(t0)\\n")'
if text.count(old) != 1:
    raise SystemExit(f"aggregate return second-chunk escape: expected one broken string, found {text.count(old)}")
path.write_text(text.replace(old, new, 1))
print("fixed generated RV64 aggregate return emitter newline escapes")
