#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one anchor, found {count}: {old[:160]!r}")
    p.write_text(text.replace(old, new, 1))


replace_once(
    "src/target/riscv64/function_symbol.h",
    "        if (fprintf(file, \".section %s\\n\", symbol->section_name) < 0) {\n",
    "        if (fprintf(file, \".section %s,\\\"ax\\\",@progbits\\n\", symbol->section_name) < 0) {\n",
)

replace_once(
    "src/target/riscv64/codegen_function.c",
    "        if (fprintf(file, \".section %s\\n\", object->section_name) < 0) {\n",
    "        if (fprintf(file,\n"
    "                    \".section %s,\\\"%s\\\",@progbits\\n\",\n"
    "                    object->section_name,\n"
    "                    object->is_read_only ? \"a\" : \"aw\") < 0) {\n",
)

print("MINIC_RISCV64_EXPLICIT_SECTION_FLAGS_V0=APPLIED")
