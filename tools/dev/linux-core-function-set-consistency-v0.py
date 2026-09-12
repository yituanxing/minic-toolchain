#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one {label} gate, found {count}")
    p.write_text(text.replace(old, new, 1))


replace_once(
    "src/compiler/compiler.c",
    "if (function->is_internal && function->is_inline && !function->is_referenced)",
    "if (function->is_internal && !function->is_referenced)",
    "remaining Core emission",
)
replace_once(
    "src/target/riscv64/codegen_function.c",
    "if (function->is_defined && function->is_internal && function->is_inline &&\n"
    "            !function->is_referenced)",
    "if (function->is_defined && function->is_internal && !function->is_referenced)",
    "RV64 codegen emission",
)
print("LINUX_CORE_FUNCTION_SET_CONSISTENCY_PATCH=APPLIED core=aligned rv64=aligned")
