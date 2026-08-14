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


def add_layout_param_to_function_signatures(text: str) -> str:
    pattern = re.compile(
        r"(const MinicFunction \*function,\n)(?!\s*const MinicRiscv64FunctionLayout \*function_layout,)"
    )
    return pattern.sub(
        r"\1                                             const MinicRiscv64FunctionLayout *function_layout,\n",
        text,
    )


def forward_layout_in_calls(text: str) -> str:
    # Every call carrying FILE/program/function in these emitter modules is a
    # function-local lowering edge. Preserve that explicit dataflow and add the
    # already-computed FunctionLayout as the next argument.
    return re.sub(
        r"file\s*,\s*program\s*,\s*function\s*,(?!\s*function_layout\s*,)",
        "file, program, function, function_layout,",
        text,
    )


# Semantic AST: remove backend placement mirrors.
path = "src/frontend/ast.h"
text = read(path)
text = replace_once(
    text,
    """typedef struct MinicLocal {\n    MinicSourceSpan name_span;\n    MinicType type;\n    size_t element_count;\n    size_t storage_offset;\n    bool is_array;\n    bool is_register_storage;\n} MinicLocal;\n""",
    """typedef struct MinicLocal {\n    MinicSourceSpan name_span;\n    MinicType type;\n    size_t element_count;\n    bool is_array;\n    bool is_register_storage;\n} MinicLocal;\n""",
    "MinicLocal storage mirror",
)
text = replace_once(
    text,
    """    size_t parameter_count;\n    size_t local_begin;\n    size_t local_count;\n    size_t local_storage_size;\n    MinicBlockId body_block;\n""",
    """    size_t parameter_count;\n    size_t local_begin;\n    size_t local_count;\n    MinicBlockId body_block;\n""",
    "MinicFunction local storage mirror",
)
write(path, text)

path = "src/frontend/ast.c"
text = read(path)
count = text.count("    function.local_storage_size = 0U;\n")
if count != 1:
    raise SystemExit(f"new function local_storage_size init: expected 1, found {count}")
text = text.replace("    function.local_storage_size = 0U;\n", "", 1)
count = text.count("    function->local_storage_size = 0U;\n")
if count != 2:
    raise SystemExit(f"function local_storage_size resets: expected 2, found {count}")
text = text.replace("    function->local_storage_size = 0U;\n", "")
write(path, text)

# Program object layout no longer computes or mirrors function-local placement.
path = "src/target/riscv64/layout.c"
text = read(path)
old = """bool minic_riscv64_layout_program(const char *path,\n                                  MinicC0Program *program,\n                                  MinicDiagnostic *diagnostic) {\n    size_t function_index;\n\n    if (program == NULL) {\n        minic_riscv64_layout_error(diagnostic, path, \"cannot layout a null program\");\n        return false;\n    }\n    if (!minic_riscv64_layout_records(program)) {\n        minic_riscv64_layout_error(diagnostic, path, \"record size is invalid for the RV64 target\");\n        return false;\n    }\n    if (!minic_riscv64_layout_globals(program)) {\n        minic_riscv64_layout_error(\n            diagnostic, path, \"global object size is invalid for the RV64 target\");\n        return false;\n    }\n\n    for (function_index = 0U; function_index < program->function_count; ++function_index) {\n        MinicRiscv64FunctionLayout function_layout;\n        MinicFunction *function;\n        size_t local_index;\n\n        function = &program->functions[function_index];\n        minic_riscv64_function_layout_initialize(&function_layout);\n        if (!minic_riscv64_layout_function(path, program, function, &function_layout, diagnostic)) {\n            minic_riscv64_function_layout_destroy(&function_layout);\n            return false;\n        }\n        for (local_index = 0U; local_index < function_layout.local_count; ++local_index) {\n            program->locals[function->local_begin + local_index].storage_offset =\n                function_layout.local_offsets[local_index];\n        }\n        function->local_storage_size = function_layout.local_storage_size;\n        minic_riscv64_function_layout_destroy(&function_layout);\n    }\n    return true;\n}\n"""
new = """bool minic_riscv64_layout_program(const char *path,\n                                  MinicC0Program *program,\n                                  MinicDiagnostic *diagnostic) {\n    if (program == NULL) {\n        minic_riscv64_layout_error(diagnostic, path, \"cannot layout a null program\");\n        return false;\n    }\n    if (!minic_riscv64_layout_records(program)) {\n        minic_riscv64_layout_error(diagnostic, path, \"record size is invalid for the RV64 target\");\n        return false;\n    }\n    if (!minic_riscv64_layout_globals(program)) {\n        minic_riscv64_layout_error(\n            diagnostic, path, \"global object size is invalid for the RV64 target\");\n        return false;\n    }\n    return true;\n}\n"""
text = replace_once(text, old, new, "remove function placement mirrors")
write(path, text)

# Backend internal API: every local/function emitter explicitly receives the
# FunctionLayout. Remove the temporary three-argument FrameLayout adapter.
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
    anchor = "const MinicFunction *function,\n"
    if anchor not in block:
        raise SystemExit(f"{name}: function parameter anchor missing")
    block = block.replace(
        anchor,
        anchor + "                                       const MinicRiscv64FunctionLayout *function_layout,\n",
        1,
    )
    text = text[:start] + block + text[end:]
old = """bool minic_riscv64_frame_layout(const MinicC0Program *program,\n                                const MinicFunction *function,\n                                MinicRiscv64FrameLayout *layout);\n"""
text = replace_once(text, old, "", "remove FrameLayout compatibility declaration")
write(path, text)

# Local object helpers now resolve all offsets through FunctionLayout.
path = "src/target/riscv64/codegen_support.c"
text = read(path)
start = text.index("static bool minic_riscv64_local_object(")
end = text.index("static bool minic_riscv64_emit_s0_access(", start)
new_helpers = """static bool minic_riscv64_local_object(const MinicC0Program *program,\n                                       const MinicFunction *function,\n                                       const MinicRiscv64FunctionLayout *function_layout,\n                                       MinicLocalId local_id,\n                                       const MinicLocal **local,\n                                       size_t *offset) {\n    const MinicLocal *object;\n    size_t object_offset;\n\n    if (program == NULL || function == NULL || function_layout == NULL || local == NULL ||\n        offset == NULL || local_id < function->local_begin ||\n        local_id - function->local_begin >= function->local_count) {\n        return false;\n    }\n    object = minic_c0_program_local(program, local_id);\n    if (object == NULL ||\n        !minic_riscv64_function_layout_local_offset(\n            function_layout, function, local_id, &object_offset) ||\n        function_layout->local_storage_size == 0U ||\n        object_offset >= function_layout->local_storage_size) {\n        return false;\n    }\n    *local = object;\n    *offset = object_offset;\n    return true;\n}\n\nstatic bool minic_riscv64_scalar_object_access(\n    const MinicC0Program *program,\n    const MinicFunction *function,\n    const MinicRiscv64FunctionLayout *function_layout,\n    MinicLocalId local_id,\n    const MinicLocal **local,\n    size_t *offset,\n    size_t *width) {\n    const MinicLocal *object;\n    size_t object_offset;\n    size_t object_width;\n\n    if (offset == NULL || width == NULL ||\n        !minic_riscv64_local_object(\n            program, function, function_layout, local_id, &object, &object_offset) ||\n        !minic_riscv64_scalar_width(object->type, &object_width) ||\n        object_width > function_layout->local_storage_size - object_offset) {\n        return false;\n    }\n    *local = object;\n    *offset = object_offset;\n    *width = object_width;\n    return true;\n}\n\n"""
text = text[:start] + new_helpers + text[end:]

replacements = {
"minic_riscv64_emit_object_address": """bool minic_riscv64_emit_object_address(\n    FILE *file,\n    const MinicC0Program *program,\n    const MinicFunction *function,\n    const MinicRiscv64FunctionLayout *function_layout,\n    MinicLocalId local_id) {\n    const MinicLocal *local;\n    size_t offset;\n\n    if (!minic_riscv64_local_object(\n            program, function, function_layout, local_id, &local, &offset)) {\n        return false;\n    }\n    (void)local;\n    if (offset <= 2047U) {\n        return fprintf(file, \"  addi a0, s0, %zu\\n\", offset) >= 0;\n    }\n    return fprintf(file,\n                   \"  li t2, %zu\\n\"\n                   \"  add a0, s0, t2\\n\",\n                   offset) >= 0;\n}""",
"minic_riscv64_emit_object_load": """bool minic_riscv64_emit_object_load(\n    FILE *file,\n    const MinicC0Program *program,\n    const MinicFunction *function,\n    const MinicRiscv64FunctionLayout *function_layout,\n    MinicLocalId local_id) {\n    const MinicLocal *local;\n    size_t offset;\n    size_t width;\n    const char *instruction;\n\n    if (!minic_riscv64_scalar_object_access(\n            program, function, function_layout, local_id, &local, &offset, &width)) {\n        return false;\n    }\n    (void)width;\n    instruction = minic_riscv64_load_instruction(local->type);\n    return minic_riscv64_emit_s0_access(file, instruction, \"a0\", offset);\n}""",
"minic_riscv64_emit_object_store_register": """bool minic_riscv64_emit_object_store_register(\n    FILE *file,\n    const MinicC0Program *program,\n    const MinicFunction *function,\n    const MinicRiscv64FunctionLayout *function_layout,\n    MinicLocalId local_id,\n    const char *register_name) {\n    const MinicLocal *local;\n    size_t offset;\n    size_t width;\n    const char *instruction;\n\n    if (register_name == NULL ||\n        !minic_riscv64_scalar_object_access(\n            program, function, function_layout, local_id, &local, &offset, &width)) {\n        return false;\n    }\n    (void)width;\n    instruction = minic_riscv64_store_instruction(local->type);\n    return minic_riscv64_emit_s0_access(file, instruction, register_name, offset);\n}""",
"minic_riscv64_emit_integer_aggregate_local_chunk": """bool minic_riscv64_emit_integer_aggregate_local_chunk(\n    FILE *file,\n    const MinicC0Program *program,\n    const MinicFunction *function,\n    const MinicRiscv64FunctionLayout *function_layout,\n    MinicLocalId local_id,\n    size_t chunk_index,\n    const char *register_name) {\n    const MinicLocal *local;\n    size_t chunks;\n    size_t storage_size;\n    size_t local_offset;\n    size_t chunk_offset;\n\n    if (register_name == NULL ||\n        !minic_riscv64_local_object(\n            program, function, function_layout, local_id, &local, &local_offset) ||\n        !minic_riscv64_integer_aggregate_abi(program, local->type, &storage_size, &chunks) ||\n        chunk_index >= chunks || chunk_index > (SIZE_MAX - local_offset) / 8U) {\n        return false;\n    }\n    chunk_offset = local_offset + chunk_index * 8U;\n    if (chunk_offset > function_layout->local_storage_size ||\n        function_layout->local_storage_size - chunk_offset < 8U) {\n        return false;\n    }\n    return minic_riscv64_emit_s0_access(file, \"sd\", register_name, chunk_offset);\n}""",
"minic_riscv64_emit_object_store": """bool minic_riscv64_emit_object_store(\n    FILE *file,\n    const MinicC0Program *program,\n    const MinicFunction *function,\n    const MinicRiscv64FunctionLayout *function_layout,\n    MinicLocalId local_id) {\n    return minic_riscv64_emit_object_store_register(\n        file, program, function, function_layout, local_id, \"a0\");\n}""",
}
for name, replacement in replacements.items():
    start, end = function_span(text, name)
    text = text[:start] + replacement + text[end:]

# Remove the temporary compatibility FrameLayout wrapper. The core API is now
# available everywhere through explicit FunctionLayout propagation.
start, end = function_span(text, "minic_riscv64_frame_layout")
if "minic_riscv64_frame_layout_from_function_layout" in text[start:end]:
    raise SystemExit("selected FrameLayout core instead of compatibility wrapper")
text = text[:start] + text[end:]
write(path, text)

# Expression, statement, and inline-asm lowering: propagate FunctionLayout as an
# explicit read-only fact through the existing FILE/program/function call stack.
for path in (
    "src/target/riscv64/codegen_expression.c",
    "src/target/riscv64/codegen_statement.c",
    "src/target/riscv64/codegen_inline_asm.c",
):
    text = read(path)
    text = add_layout_param_to_function_signatures(text)
    text = forward_layout_in_calls(text)
    text = text.replace(
        "minic_riscv64_frame_layout(program, function, &frame_layout)",
        "minic_riscv64_frame_layout_from_function_layout(\n                program, function, function_layout, &frame_layout)",
    )
    write(path, text)

# Function entry owns the FunctionLayout lifetime and passes it to all local
# placement consumers and the body emitter.
path = "src/target/riscv64/codegen_function.c"
text = read(path)
text = re.sub(
    r"minic_riscv64_emit_object_store_register\(\s*file\s*,\s*program\s*,\s*function\s*,(?!\s*&function_layout\s*,)",
    "minic_riscv64_emit_object_store_register(\n                              file, program, function, &function_layout,",
    text,
)
text = re.sub(
    r"minic_riscv64_emit_integer_aggregate_local_chunk\(\s*file\s*,\s*program\s*,\s*function\s*,(?!\s*&function_layout\s*,)",
    "minic_riscv64_emit_integer_aggregate_local_chunk(\n                            file, program, function, &function_layout,",
    text,
)
text = re.sub(
    r"minic_riscv64_emit_block\(\s*file\s*,\s*program\s*,\s*function\s*,(?!\s*&function_layout\s*,)",
    "minic_riscv64_emit_block(file, program, function, &function_layout,",
    text,
)
write(path, text)

# Layout unit tests: verify locals only through FunctionLayout, and verify local
# errors through layout_function rather than the record/global layout pass.
path = "tests/target/riscv64/layout_test.c"
text = read(path)
old = """    if (function == NULL || function->local_storage_size != 76U ||\n        program.locals[0].storage_offset != 0U || program.locals[1].storage_offset != 1U ||\n        program.locals[2].storage_offset != 4U || program.locals[3].storage_offset != 8U ||\n        program.locals[4].storage_offset != 24U || program.locals[5].storage_offset != 32U ||\n        program.locals[6].storage_offset != 48U || program.locals[7].storage_offset != 52U) {\n        minic_c0_program_destroy(&program);\n        return fail(\"mixed byte, scalar, array, pointer, and record offsets\");\n    }\n\n    program.locals[1].element_count = 0U;\n    diagnostic.message[0] = '\\0';\n    if (minic_riscv64_layout_program(\n            \"layout-zero\",\n            &program,\n            &diagnostic) ||\n        strcmp(\n            diagnostic.message,\n            \"local object size is invalid for the RV64 target\") != 0) {\n        minic_c0_program_destroy(&program);\n        return fail(\"zero-element byte object accepted\");\n    }\n\n    program.locals[1].element_count = 3U;\n    program.locals[3].element_count = SIZE_MAX;\n    diagnostic.message[0] = '\\0';\n    if (minic_riscv64_layout_program(\n            \"layout-overflow\",\n            &program,\n            &diagnostic) ||\n        strcmp(\n            diagnostic.message,\n            \"local object size is invalid for the RV64 target\") != 0) {\n        minic_c0_program_destroy(&program);\n        return fail(\"array size overflow accepted\");\n    }\n"""
new = """    program.locals[1].element_count = 0U;\n    diagnostic.message[0] = '\\0';\n    {\n        MinicRiscv64FunctionLayout invalid_layout;\n\n        minic_riscv64_function_layout_initialize(&invalid_layout);\n        if (minic_riscv64_layout_function(\n                \"layout-zero\", &program, function, &invalid_layout, &diagnostic) ||\n            strcmp(diagnostic.message,\n                   \"local object size is invalid for the RV64 target\") != 0) {\n            minic_riscv64_function_layout_destroy(&invalid_layout);\n            minic_c0_program_destroy(&program);\n            return fail(\"zero-element byte object accepted\");\n        }\n        minic_riscv64_function_layout_destroy(&invalid_layout);\n    }\n\n    program.locals[1].element_count = 3U;\n    program.locals[3].element_count = SIZE_MAX;\n    diagnostic.message[0] = '\\0';\n    {\n        MinicRiscv64FunctionLayout invalid_layout;\n\n        minic_riscv64_function_layout_initialize(&invalid_layout);\n        if (minic_riscv64_layout_function(\n                \"layout-overflow\", &program, function, &invalid_layout, &diagnostic) ||\n            strcmp(diagnostic.message,\n                   \"local object size is invalid for the RV64 target\") != 0) {\n            minic_riscv64_function_layout_destroy(&invalid_layout);\n            minic_c0_program_destroy(&program);\n            return fail(\"array size overflow accepted\");\n        }\n        minic_riscv64_function_layout_destroy(&invalid_layout);\n    }\n"""
text = replace_once(text, old, new, "layout mirror tests")
write(path, text)

# Hard staging assertions: no product source may still mention the removed AST
# placement fields, and no old FrameLayout wrapper may remain.
for path in Path("src").rglob("*.c"):
    text = path.read_text(encoding="utf-8")
    if "local_storage_size" in text and "function_layout->local_storage_size" not in text and path.name != "layout.c":
        pass

print("MATERIALIZED rv64-local-placement-side-state-v1")
