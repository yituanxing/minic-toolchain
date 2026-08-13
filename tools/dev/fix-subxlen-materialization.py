#!/usr/bin/env python3
from pathlib import Path

path = Path("src/target/riscv64/codegen_support.c")
text = path.read_text()
replacements = {
    '"  ld %s, %zu(%s)\n",': '"  ld %s, %zu(%s)\\n",',
    '"  lwu %s, %zu(%s)\n",': '"  lwu %s, %zu(%s)\\n",',
    '"  lhu %s, %zu(%s)\n",': '"  lhu %s, %zu(%s)\\n",',
    '"  lbu %s, %zu(%s)\n",': '"  lbu %s, %zu(%s)\\n",',
    '"  li %s, 0\n",': '"  li %s, 0\\n",',
    '"  lbu t6, %zu(%s)\n",': '"  lbu t6, %zu(%s)\\n",',
    '"  slli t6, t6, %zu\n",': '"  slli t6, t6, %zu\\n",',
    '"  or %s, %s, t6\n",': '"  or %s, %s, t6\\n",',
    '"  mv t1, %s\n",': '"  mv t1, %s\\n",',
    '"  srli t1, t1, 8\n"': '"  srli t1, t1, 8\\n"',
}
changed = 0
for old, new in replacements.items():
    if old in text:
        text = text.replace(old, new)
        changed += 1
if changed < 8:
    raise SystemExit(f"expected sub-XLEN escape repairs, found {changed}")
path.write_text(text)
print(f"normalized sub-XLEN C string escapes={changed}")
