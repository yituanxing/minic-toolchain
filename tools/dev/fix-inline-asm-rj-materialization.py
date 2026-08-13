#!/usr/bin/env python3
from pathlib import Path
import runpy

path = Path("src/target/riscv64/codegen_inline_asm.c")
text = path.read_text()
old = "assign_operand_registers(inline_asm, operand_registers, operand_count)"
new = "assign_operand_registers(inline_asm, program, operand_registers, operand_count)"
if old in text:
    text = text.replace(old, new)
elif new not in text:
    raise SystemExit("assign_operand_registers call anchor not found")
path.write_text(text)

runpy.run_path("tools/dev/materialize-cleanup-cast-remap.py", run_name="__main__")
runpy.run_path("tools/dev/materialize-record-return-source.py", run_name="__main__")
runpy.run_path("tools/dev/materialize-call-trace.py", run_name="__main__")
