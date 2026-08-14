#!/usr/bin/env python3
from pathlib import Path
import re


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


def replace_function(text: str, name: str, replacement: str) -> str:
    start, end = function_span(text, name)
    return text[:start] + replacement + text[end:]


def add_layout_param_to_signature_block(block: str, name: str) -> str:
    if "const MinicRiscv64FunctionLayout *function_layout" in block:
        return block
    anchor = "const MinicFunction *function,"
    if anchor not in block:
        raise SystemExit(f"{name}: function parameter anchor missing")
    return block.replace(
        anchor,
        anchor + "\n                                       const MinicRiscv64FunctionLayout *function_layout,",
        1,
    )


def add_layout_param_to_function_signatures(text: str) -> str:
    pattern = re.compile(
        r"(const MinicFunction \*function,\n)(?!\s*const MinicRiscv64FunctionLayout \*function_layout,)"
    )
    return pattern.sub(
        r"\1                                             const MinicRiscv64FunctionLayout *function_layout,\n",
        text,
    )


def forward_layout_in_calls(text: str, layout_expr: str = "function_layout") -> str:
    return re.sub(
        r"file\s*,\s*program\s*,\s*function\s*,(?!\s*(?:function_layout|&function_layout)\s*,)",
        f"file, program, function, {layout_expr},",
        text,
    )


# Header: preserve discovery-only helpers while adding FunctionLayout to every
# function-local lowering API.
path = "src/target/riscv64/codegen_internal.h"
text = read(path)
for name in (
    "minic_riscv64_emit_object_address",
    "minic_riscv64_emit_object_load",
    "minic_riscv64_emit_object_store",
    "minic_riscv64_emit_object_store_register",
    "minic_riscv64_emit_integer_aggregate_local_chunk",
    "minic_riscv64_emit_lvalue_address",
    "minic_riscv64_emit_address_backed_record_value",
    "minic_riscv64_emit_record_copy_value",
    "minic_riscv64_emit_expression",
    "minic_riscv64_emit_inline_asm",
    "minic_riscv64_emit_block",
):
    start = text.index(name + "(")
    end = text.index(");", start) + 2
    block = text[start:end]
    block = add_layout_param_to_signature_block(block, name)
    text = text[:start] + block + text[end:]

frame_pattern = re.compile(
    r"bool minic_riscv64_frame_layout\(const MinicC0Program \*program,\s*"
    r"const MinicFunction \*function,\s*MinicRiscv64FrameLayout \*layout\);",
    re.S,
)
text, count = frame_pattern.subn(
    """bool minic_riscv64_frame_layout_from_function_layout(
    const MinicC0Program *program,
    const MinicFunction *function,
    const MinicRiscv64FunctionLayout *function_layout,
    MinicRiscv64FrameLayout *layout);""",
    text,
    count=1,
)
if count != 1:
    raise SystemExit(f"codegen_internal.h: expected one legacy FrameLayout declaration, found {count}")
write(path, text)


# Support: the local placement owner changes, but preserve discovery's sub-XLEN
# aggregate chunk semantics.
path = "src/target/riscv64/codegen_support.c"
text = read(path)

text = replace_function(
    text,
    "minic_riscv64_local_object",
    """static bool minic_riscv64_local_object(const MinicC0Program *program,
                                       const MinicFunction *function,
                                       const MinicRiscv64FunctionLayout *function_layout,
                                       MinicLocalId local_id,
                                       const MinicLocal **local,
                                       size_t *offset) {
    const MinicLocal *object;
    size_t object_offset;

    if (program == NULL || function == NULL || function_layout == NULL || local == NULL ||
        offset == NULL || local_id < function->local_begin ||
        local_id - function->local_begin >= function->local_count) {
        return false;
    }
    object = minic_c0_program_local(program, local_id);
    if (object == NULL ||
        !minic_riscv64_function_layout_local_offset(
            function_layout, function, local_id, &object_offset) ||
        function_layout->local_storage_size == 0U ||
        object_offset >= function_layout->local_storage_size) {
        return false;
    }
    *local = object;
    *offset = object_offset;
    return true;
}""",
)

text = replace_function(
    text,
    "minic_riscv64_scalar_object_access",
    """static bool minic_riscv64_scalar_object_access(
    const MinicC0Program *program,
    const MinicFunction *function,
    const MinicRiscv64FunctionLayout *function_layout,
    MinicLocalId local_id,
    const MinicLocal **local,
    size_t *offset,
    size_t *width) {
    const MinicLocal *object;
    size_t object_offset;
    size_t object_width;

    if (offset == NULL || width == NULL ||
        !minic_riscv64_local_object(
            program, function, function_layout, local_id, &object, &object_offset) ||
        !minic_riscv64_scalar_width(object->type, &object_width) ||
        object_width > function_layout->local_storage_size - object_offset) {
        return false;
    }
    *local = object;
    *offset = object_offset;
    *width = object_width;
    return true;
}""",
)

text = replace_function(
    text,
    "minic_riscv64_emit_object_address",
    """bool minic_riscv64_emit_object_address(
    FILE *file,
    const MinicC0Program *program,
    const MinicFunction *function,
    const MinicRiscv64FunctionLayout *function_layout,
    MinicLocalId local_id) {
    const MinicLocal *local;
    size_t offset;

    if (!minic_riscv64_local_object(
            program, function, function_layout, local_id, &local, &offset)) {
        return false;
    }
    (void)local;
    if (offset <= 2047U) {
        return fprintf(file, "  addi a0, s0, %zu\\n", offset) >= 0;
    }
    return fprintf(file,
                   "  li t2, %zu\\n"
                   "  add a0, s0, t2\\n",
                   offset) >= 0;
}""",
)

text = replace_function(
    text,
    "minic_riscv64_emit_object_load",
    """bool minic_riscv64_emit_object_load(
    FILE *file,
    const MinicC0Program *program,
    const MinicFunction *function,
    const MinicRiscv64FunctionLayout *function_layout,
    MinicLocalId local_id) {
    const MinicLocal *local;
    size_t offset;
    size_t width;
    const char *instruction;

    if (!minic_riscv64_scalar_object_access(
            program, function, function_layout, local_id, &local, &offset, &width)) {
        return false;
    }
    (void)width;
    instruction = minic_riscv64_load_instruction(local->type);
    return minic_riscv64_emit_s0_access(file, instruction, "a0", offset);
}""",
)

text = replace_function(
    text,
    "minic_riscv64_emit_object_store_register",
    """bool minic_riscv64_emit_object_store_register(
    FILE *file,
    const MinicC0Program *program,
    const MinicFunction *function,
    const MinicRiscv64FunctionLayout *function_layout,
    MinicLocalId local_id,
    const char *register_name) {
    const MinicLocal *local;
    size_t offset;
    size_t width;
    const char *instruction;

    if (register_name == NULL ||
        !minic_riscv64_scalar_object_access(
            program, function, function_layout, local_id, &local, &offset, &width)) {
        return false;
    }
    (void)width;
    instruction = minic_riscv64_store_instruction(local->type);
    return minic_riscv64_emit_s0_access(file, instruction, register_name, offset);
}""",
)

text = replace_function(
    text,
    "minic_riscv64_emit_integer_aggregate_local_chunk",
    """bool minic_riscv64_emit_integer_aggregate_local_chunk(
    FILE *file,
    const MinicC0Program *program,
    const MinicFunction *function,
    const MinicRiscv64FunctionLayout *function_layout,
    MinicLocalId local_id,
    size_t chunk_index,
    const char *register_name) {
    const MinicLocal *local;
    const char *instruction;
    size_t chunk_count;
    size_t chunk_offset;
    size_t chunk_size;
    size_t index;
    size_t local_offset;
    size_t storage_size;

    if (register_name == NULL ||
        !minic_riscv64_local_object(
            program, function, function_layout, local_id, &local, &local_offset) ||
        !minic_riscv64_integer_aggregate_abi(program, local->type, &storage_size, &chunk_count) ||
        chunk_index >= chunk_count || chunk_index > (SIZE_MAX - local_offset) / 8U) {
        return false;
    }
    chunk_offset = local_offset + chunk_index * 8U;
    chunk_size = storage_size - chunk_index * 8U;
    if (chunk_size > 8U) {
        chunk_size = 8U;
    }
    if (chunk_offset > function_layout->local_storage_size ||
        chunk_size > function_layout->local_storage_size - chunk_offset) {
        return false;
    }
    instruction = chunk_size == 8U   ? "sd"
                  : chunk_size == 4U ? "sw"
                  : chunk_size == 2U ? "sh"
                  : chunk_size == 1U ? "sb"
                                     : NULL;
    if (instruction != NULL) {
        return minic_riscv64_emit_s0_access(file, instruction, register_name, chunk_offset);
    }
    if (fprintf(file, "  mv t1, %s\\n", register_name) < 0) {
        return false;
    }
    for (index = 0U; index < chunk_size; ++index) {
        if (!minic_riscv64_emit_s0_access(file, "sb", "t1", chunk_offset + index) ||
            (index + 1U < chunk_size && fprintf(file, "  srli t1, t1, 8\\n") < 0)) {
            return false;
        }
    }
    return true;
}""",
)

text = replace_function(
    text,
    "minic_riscv64_emit_object_store",
    """bool minic_riscv64_emit_object_store(
    FILE *file,
    const MinicC0Program *program,
    const MinicFunction *function,
    const MinicRiscv64FunctionLayout *function_layout,
    MinicLocalId local_id) {
    return minic_riscv64_emit_object_store_register(
        file, program, function, function_layout, local_id, "a0");
}""",
)

frame_start, frame_end = function_span(text, "minic_riscv64_frame_layout")
frame = text[frame_start:frame_end]
frame = frame.replace(
    "bool minic_riscv64_frame_layout(const MinicC0Program *program,",
    "bool minic_riscv64_frame_layout_from_function_layout(const MinicC0Program *program,",
    1,
)
function_anchor = "const MinicFunction *function,\n"
if function_anchor not in frame:
    raise SystemExit("codegen_support.c: FrameLayout function parameter anchor missing")
frame = frame.replace(
    function_anchor,
    function_anchor + "                                const MinicRiscv64FunctionLayout *function_layout,\n",
    1,
)
frame = frame.replace(
    "if (program == NULL || function == NULL || layout == NULL ||",
    "if (program == NULL || function == NULL || function_layout == NULL || layout == NULL ||",
    1,
)
if "function->local_storage_size" not in frame:
    raise SystemExit("codegen_support.c: legacy FrameLayout storage source missing")
frame = frame.replace("function->local_storage_size", "function_layout->local_storage_size")
text = text[:frame_start] + frame + text[frame_end:]
write(path, text)


# Expression and statement discovery code keep their diagnostics and extended
# aggregate semantics. Only thread FunctionLayout and switch FrameLayout calls.
for path in (
    "src/target/riscv64/codegen_expression.c",
    "src/target/riscv64/codegen_statement.c",
):
    text = read(path)
    text = add_layout_param_to_function_signatures(text)
    text = forward_layout_in_calls(text)
    text = re.sub(
        r"minic_riscv64_frame_layout\(\s*program\s*,\s*function\s*,\s*&frame_layout\s*\)",
        "minic_riscv64_frame_layout_from_function_layout(\n                program, function, function_layout, &frame_layout)",
        text,
    )
    write(path, text)


# Function emission owns one FunctionLayout. Preserve discovery's failure
# diagnostics while making them consume the new side state.
path = "src/target/riscv64/codegen_function.c"
text = read(path)
start, end = function_span(text, "minic_riscv64_emit_function")
block = text[start:end]
if "MinicRiscv64FunctionLayout function_layout;" not in block:
    anchor = "    MinicRiscv64FrameLayout frame_layout;\n"
    if anchor not in block:
        raise SystemExit("codegen_function.c: FrameLayout local declaration missing")
    block = block.replace(
        anchor,
        "    MinicRiscv64FunctionLayout function_layout;\n" + anchor,
        1,
    )
old_frame = "    if (!minic_riscv64_frame_layout(program, function, &frame_layout)) {\n"
if block.count(old_frame) != 1:
    raise SystemExit(
        f"codegen_function.c: expected one legacy frame-layout gate, found {block.count(old_frame)}"
    )
block = block.replace(
    old_frame,
    """    minic_riscv64_function_layout_initialize(&function_layout);
    if (!minic_riscv64_layout_function(NULL, program, function, &function_layout, NULL)) {
        fprintf(stderr, "CODEGEN_FUNCTION_ENTRY function-layout name=%s\\n", function->name);
        return false;
    }
    if (!minic_riscv64_frame_layout_from_function_layout(
            program, function, &function_layout, &frame_layout)) {
""",
    1,
)
# The first return after the FrameLayout diagnostic block is its failure exit.
frame_gate = block.index("if (!minic_riscv64_frame_layout_from_function_layout(")
brace = block.index("{", frame_gate)
depth = 0
frame_gate_end = None
for index in range(brace, len(block)):
    if block[index] == "{":
        depth += 1
    elif block[index] == "}":
        depth -= 1
        if depth == 0:
            frame_gate_end = index + 1
            break
if frame_gate_end is None:
    raise SystemExit("codegen_function.c: unterminated FrameLayout diagnostic gate")
frame_gate_block = block[frame_gate:frame_gate_end]
return_pos = frame_gate_block.rfind("        return false;")
if return_pos < 0:
    raise SystemExit("codegen_function.c: FrameLayout failure return missing")
frame_gate_block = (
    frame_gate_block[:return_pos]
    + "        minic_riscv64_function_layout_destroy(&function_layout);\n"
    + frame_gate_block[return_pos:]
)
block = block[:frame_gate] + frame_gate_block + block[frame_gate_end:]

block = forward_layout_in_calls(block, "&function_layout")

symbol_old = """    if (symbol_name == NULL || symbol_name[0] == '\\0') {
        return false;
    }
"""
if symbol_old in block:
    block = block.replace(
        symbol_old,
        """    if (symbol_name == NULL || symbol_name[0] == '\\0') {
        minic_riscv64_function_layout_destroy(&function_layout);
        return false;
    }
""",
        1,
    )

last_return = block.rfind("    return success;")
if last_return < 0:
    raise SystemExit("codegen_function.c: emit_function final return missing")
block = (
    block[:last_return]
    + "    minic_riscv64_function_layout_destroy(&function_layout);\n"
    + block[last_return:]
)
text = text[:start] + block + text[end:]
write(path, text)


# Hybrid-specific hard assertions. Discovery diagnostics are allowed; legacy
# local placement ownership is not.
for path in (
    "src/target/riscv64/codegen_expression.c",
    "src/target/riscv64/codegen_function.c",
    "src/target/riscv64/codegen_internal.h",
    "src/target/riscv64/codegen_statement.c",
    "src/target/riscv64/codegen_support.c",
):
    text = read(path)
    if "<<<<<<<" in text or ">>>>>>>" in text:
        raise SystemExit(f"{path}: merge marker remains")

support = read("src/target/riscv64/codegen_support.c")
if "local->storage_offset" in support or "object->storage_offset" in support:
    raise SystemExit("codegen_support.c: legacy local storage offset owner remains")
if "function->local_storage_size" in support:
    raise SystemExit("codegen_support.c: legacy function local storage owner remains")
if "minic_riscv64_frame_layout(" in support:
    raise SystemExit("codegen_support.c: legacy FrameLayout wrapper remains")

print("MATERIALIZED rv64-local-placement-hybrid")
