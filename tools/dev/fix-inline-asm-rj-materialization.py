#!/usr/bin/env python3
from pathlib import Path

path = Path("src/target/riscv64/codegen_inline_asm.c")
text = path.read_text()
old = "assign_operand_registers(inline_asm, operand_registers, operand_count)"
new = "assign_operand_registers(inline_asm, program, operand_registers, operand_count)"
if old in text:
    text = text.replace(old, new)
elif new not in text:
    raise SystemExit("assign_operand_registers call anchor not found")
path.write_text(text)
