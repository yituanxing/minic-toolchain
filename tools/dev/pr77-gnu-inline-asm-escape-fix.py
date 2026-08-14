#!/usr/bin/env python3
from pathlib import Path

path = Path("src/target/riscv64/codegen_statement.c")
text = path.read_text()
old = 'fprintf(file, "  %s\n", inline_asm->template_text)'
new = 'fprintf(file, "  %s\\n", inline_asm->template_text)'
if text.count(old) != 1:
    raise SystemExit(f"inline asm emitter escape: expected one broken generated string, found {text.count(old)}")
path.write_text(text.replace(old, new, 1))
print("fixed generated inline asm emitter newline escape")
