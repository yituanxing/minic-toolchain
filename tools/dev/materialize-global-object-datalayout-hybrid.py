#!/usr/bin/env python3
from pathlib import Path


def read(path: str) -> str:
    return Path(path).read_text(encoding="utf-8")


def write(path: str, text: str) -> None:
    Path(path).write_text(text.rstrip() + "\n", encoding="utf-8")


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
    raise SystemExit(f"unterminated function: {name}")


path = "src/target/riscv64/codegen_function.c"
text = read(path)

# Keep all discovery-only global emission semantics (notably zero-sized record
# definitions). This adapter changes only the owner of derived object size/alignment.
for name in (
    "emit_symbol_relocs",
    "minic_riscv64_emit_direct_record_values",
    "minic_riscv64_emit_record_values",
    "minic_riscv64_emit_record_array_values",
    "minic_riscv64_emit_global_object",
):
    start, end = function_span(text, name)
    block = text[start:end]
    needs_size = "object->storage_size" in block
    needs_alignment = "object->alignment" in block
    if not needs_size and not needs_alignment:
        raise SystemExit(f"{name}: expected discovery object-layout cache consumer")
    if "minic_data_layout_global_object(" in block:
        raise SystemExit(f"{name}: object layout query already present unexpectedly")

    brace = block.index("{") + 1
    query = """
    size_t object_alignment;
    size_t storage_size;

    if (!minic_data_layout_global_object(minic_default_data_layout(),
                                         program,
                                         object,
                                         &storage_size,
                                         &object_alignment)) {
        return false;
    }
"""
    if not needs_alignment:
        query += "    (void)object_alignment;\n"
    block = block[:brace] + query + block[brace:]
    block = block.replace("object->storage_size", "storage_size")
    block = block.replace("object->alignment", "object_alignment")
    text = text[:start] + block + text[end:]

# Discovery adds a helper that recognizes GNU zero-sized record definitions.
# Preserve that exact predicate, but derive the zero-size fact from DataLayout
# rather than a field that no longer exists in the semantic object.
start, end = function_span(text, "minic_riscv64_zero_size_record_definition")
block = text[start:end]
if "object->storage_size" not in block:
    raise SystemExit("zero-size record helper no longer has expected cache read")
brace = block.index("{") + 1
query = """
    size_t object_alignment;
    size_t storage_size;

    if (program == NULL || object == NULL ||
        !minic_data_layout_global_object(minic_default_data_layout(),
                                         program,
                                         object,
                                         &storage_size,
                                         &object_alignment)) {
        return false;
    }
    (void)object_alignment;
"""
block = block[:brace] + query + block[brace:]
block = block.replace("program == NULL || object == NULL || object->storage_size != 0U ||", "storage_size != 0U ||", 1)
text = text[:start] + block + text[end:]

if "object->storage_size" in text or "object->alignment" in text:
    raise SystemExit("discovery global-object layout cache consumer remains")
if "zero_size_record_definition" not in text:
    raise SystemExit("discovery zero-size record emission semantics were lost")
if "minic_data_layout_global_object(" not in text:
    raise SystemExit("global-object DataLayout query was not installed")

write(path, text)
print("MATERIALIZED global-object-datalayout-hybrid")
