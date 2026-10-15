#!/usr/bin/env python3
from pathlib import Path

PATH = Path("src/target/riscv64/core_codegen.c")
text = PATH.read_text()

old = '''        if (!core_instruction_supported(program, function, &function->instructions[index])) {
            return false;
        }
'''
new = '''        if (!core_instruction_supported(program, function, &function->instructions[index])) {
            fprintf(stderr,
                    "M167B_PREFLIGHT reason=instruction index=%zu kind=%d\\n",
                    index, (int)function->instructions[index].kind);
            return false;
        }
'''
count = text.count(old)
if count != 1:
    raise SystemExit(f"M167b instruction trace: expected 1 preflight seam, got {count}")
text = text.replace(old, new, 1)
PATH.write_text(text)
print("M167B_INSTRUCTION_TRACE_APPLIED")
