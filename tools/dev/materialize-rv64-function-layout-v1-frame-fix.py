#!/usr/bin/env python3
from pathlib import Path
import re


def read(path: str) -> str:
    return Path(path).read_text(encoding="utf-8")


def write(path: str, text: str) -> None:
    Path(path).write_text(text.rstrip() + "\n", encoding="utf-8")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)


path = "src/target/riscv64/codegen_internal.h"
text = read(path)
old = """bool minic_riscv64_frame_layout(const MinicC0Program *program,\n                                const MinicFunction *function,\n                                const MinicRiscv64FunctionLayout *function_layout,\n                                MinicRiscv64FrameLayout *layout);\n"""
new = """bool minic_riscv64_frame_layout_from_function_layout(\n    const MinicC0Program *program,\n    const MinicFunction *function,\n    const MinicRiscv64FunctionLayout *function_layout,\n    MinicRiscv64FrameLayout *layout);\nbool minic_riscv64_frame_layout(const MinicC0Program *program,\n                                const MinicFunction *function,\n                                MinicRiscv64FrameLayout *layout);\n"""
text = replace_once(text, old, new, "codegen_internal frame APIs")
write(path, text)

path = "src/target/riscv64/codegen_support.c"
text = read(path)
text = replace_once(
    text,
    "bool minic_riscv64_frame_layout(const MinicC0Program *program,\n",
    "bool minic_riscv64_frame_layout_from_function_layout(const MinicC0Program *program,\n",
    "frame core rename",
)
pattern = re.compile(
    r"bool minic_riscv64_frame_layout_from_function_layout\(const MinicC0Program \*program,.*?\n\}",
    re.S,
)
match = pattern.search(text)
if match is None:
    raise SystemExit("frame core block missing")
wrapper = """\n\nbool minic_riscv64_frame_layout(const MinicC0Program *program,\n                                const MinicFunction *function,\n                                MinicRiscv64FrameLayout *layout) {\n    MinicRiscv64FunctionLayout function_layout;\n    bool success;\n\n    minic_riscv64_function_layout_initialize(&function_layout);\n    if (!minic_riscv64_layout_function(NULL, program, function, &function_layout, NULL)) {\n        return false;\n    }\n    success = minic_riscv64_frame_layout_from_function_layout(\n        program, function, &function_layout, layout);\n    minic_riscv64_function_layout_destroy(&function_layout);\n    return success;\n}\n"""
text = text[: match.end()] + wrapper + text[match.end() :]
write(path, text)

path = "src/target/riscv64/codegen_function.c"
text = read(path)
text = replace_once(
    text,
    "!minic_riscv64_frame_layout(program, function, &function_layout, &frame_layout)",
    "!minic_riscv64_frame_layout_from_function_layout(\n            program, function, &function_layout, &frame_layout)",
    "emit_function frame core call",
)
write(path, text)

print("MATERIALIZED rv64-function-layout-v1-frame-fix")
