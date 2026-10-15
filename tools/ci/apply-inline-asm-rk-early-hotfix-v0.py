#!/usr/bin/env python3
from pathlib import Path

p = Path("src/core/core_lower_asm.c")
text = p.read_text()
old = "block->terminated || block->instruction_count < 7U"
new = "block->has_terminator || block->instruction_count < 7U"
if text.count(old) != 1:
    raise SystemExit(f"expected one rK rewind terminator guard, found {text.count(old)}")
p.write_text(text.replace(old, new, 1))
print("MINIC_INLINE_ASM_RK_EARLY_HOTFIX_V0=APPLIED")
