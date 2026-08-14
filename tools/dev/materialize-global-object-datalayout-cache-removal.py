#!/usr/bin/env python3
from pathlib import Path


def read(path: str) -> str:
    return Path(path).read_text(encoding="utf-8")


def write(path: str, text: str) -> None:
    Path(path).write_text(text.rstrip() + "\n", encoding="utf-8")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)


def function_span(text: str, name: str) -> tuple[int, int]:
    marker = name + "("
    marker_index = text.index(marker)
    start = text.rfind("\n", 0, marker_index) + 1
    brace = text.index("{", marker_index)
    depth = 0
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return start, index + 1
    raise SystemExit(f"unterminated function {name}")


# Semantic object nodes retain object policy inputs but not derived target answers.
path = "src/frontend/ast.h"
text = read(path)
text = replace_once(
    text,
    """    size_t explicit_alignment;
    size_t storage_size;
    size_t alignment;
    MinicSymbolVisibility visibility;
""",
    """    size_t explicit_alignment;
    MinicSymbolVisibility visibility;
""",
    "global object derived layout fields",
)
write(path, text)

# Global layout pass validates the canonical query but no longer materializes a cache.
path = "src/target/riscv64/layout.c"
text = read(path)
old = """        if (!minic_data_layout_global_object(
                minic_default_data_layout(), program, object, &storage_size, &alignment)) {
            return false;
        }
        object->storage_size = storage_size;
        object->alignment = alignment;
"""
new = """        if (!minic_data_layout_global_object(
                minic_default_data_layout(), program, object, &storage_size, &alignment)) {
            return false;
        }
"""
text = replace_once(text, old, new, "global layout cache writes")
write(path, text)

# RV64 global emission queries canonical object layout wherever a helper needs the extent.
path = "src/target/riscv64/codegen_function.c"
text = read(path)

for name in (
    "emit_symbol_relocs",
    "minic_riscv64_emit_direct_record_values",
    "minic_riscv64_emit_record_values",
    "minic_riscv64_emit_record_array_values",
):
    start, end = function_span(text, name)
    block = text[start:end]
    brace = block.index("{") + 1
    prefix = """
    size_t object_alignment;
    size_t storage_size;

    if (!minic_data_layout_global_object(minic_default_data_layout(),
                                         program,
                                         object,
                                         &storage_size,
                                         &object_alignment)) {
        return false;
    }
    (void)object_alignment;
"""
    block = block[:brace] + prefix + block[brace:]
    if "object->storage_size" not in block:
        raise SystemExit(f"{name}: expected global storage cache consumer")
    block = block.replace("object->storage_size", "storage_size")
    text = text[:start] + block + text[end:]

start, end = function_span(text, "minic_riscv64_emit_global_object")
block = text[start:end]
brace = block.index("{") + 1
prefix = """
    size_t object_alignment;
    size_t storage_size;
"""
block = block[:brace] + prefix + block[brace:]
anchor = """    if (file == NULL || program == NULL || object == NULL || object->name_length == 0U ||
"""
query = """    if (!minic_data_layout_global_object(minic_default_data_layout(),
                                         program,
                                         object,
                                         &storage_size,
                                         &object_alignment)) {
        return false;
    }

""" + anchor
block = replace_once(block, anchor, query, "global emitter object query")
if "object->storage_size" not in block or "object->alignment" not in block:
    raise SystemExit("global emitter expected size/alignment cache consumers")
block = block.replace("object->storage_size", "storage_size")
block = block.replace("object->alignment", "object_alignment")
text = text[:start] + block + text[end:]

if "object->storage_size" in text or "object->alignment" in text:
    raise SystemExit("codegen_function.c: global object layout cache consumer remains")
write(path, text)

print("MATERIALIZED global-object-datalayout-cache-removal")
