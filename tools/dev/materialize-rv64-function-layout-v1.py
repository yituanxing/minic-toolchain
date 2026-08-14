#!/usr/bin/env python3
from pathlib import Path
import re


def read(path: str) -> str:
    return Path(path).read_text(encoding="utf-8")


def write(path: str, text: str) -> None:
    Path(path).write_text(text, encoding="utf-8")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)


# layout.h: introduce the backend-owned local placement result.
path = "src/target/riscv64/layout.h"
text = read(path)
old = """bool minic_riscv64_type_layout(const MinicC0Program *program,\n                               MinicType type,\n                               size_t *size,\n                               size_t *alignment);\nbool minic_riscv64_layout_program(const char *path,\n                                  MinicC0Program *program,\n                                  MinicDiagnostic *diagnostic);\n"""
new = """bool minic_riscv64_type_layout(const MinicC0Program *program,\n                               MinicType type,\n                               size_t *size,\n                               size_t *alignment);\n\ntypedef struct MinicRiscv64FunctionLayout {\n    size_t *local_offsets;\n    size_t local_count;\n    size_t local_storage_size;\n} MinicRiscv64FunctionLayout;\n\nvoid minic_riscv64_function_layout_initialize(MinicRiscv64FunctionLayout *layout);\nvoid minic_riscv64_function_layout_destroy(MinicRiscv64FunctionLayout *layout);\nbool minic_riscv64_layout_function(const char *path,\n                                   const MinicC0Program *program,\n                                   const MinicFunction *function,\n                                   MinicRiscv64FunctionLayout *layout,\n                                   MinicDiagnostic *diagnostic);\nbool minic_riscv64_function_layout_local_offset(const MinicRiscv64FunctionLayout *layout,\n                                                const MinicFunction *function,\n                                                MinicLocalId local_id,\n                                                size_t *offset);\n\nbool minic_riscv64_layout_program(const char *path,\n                                  MinicC0Program *program,\n                                  MinicDiagnostic *diagnostic);\n"""
text = replace_once(text, old, new, "layout.h API")
write(path, text)

# layout.c: make function-local placement a reusable side-state computation.
path = "src/target/riscv64/layout.c"
text = read(path)
text = replace_once(text, "#include <stdint.h>\n#include <stdio.h>\n", "#include <stdint.h>\n#include <stdio.h>\n#include <stdlib.h>\n", "layout.c stdlib")
anchor = """bool minic_riscv64_layout_program(const char *path,\n                                  MinicC0Program *program,\n                                  MinicDiagnostic *diagnostic) {\n"""
insert = """void minic_riscv64_function_layout_initialize(MinicRiscv64FunctionLayout *layout) {\n    if (layout == NULL) {\n        return;\n    }\n    layout->local_offsets = NULL;\n    layout->local_count = 0U;\n    layout->local_storage_size = 0U;\n}\n\nvoid minic_riscv64_function_layout_destroy(MinicRiscv64FunctionLayout *layout) {\n    if (layout == NULL) {\n        return;\n    }\n    free(layout->local_offsets);\n    minic_riscv64_function_layout_initialize(layout);\n}\n\nbool minic_riscv64_function_layout_local_offset(const MinicRiscv64FunctionLayout *layout,\n                                                const MinicFunction *function,\n                                                MinicLocalId local_id,\n                                                size_t *offset) {\n    size_t local_index;\n\n    if (layout == NULL || function == NULL || offset == NULL ||\n        layout->local_count != function->local_count || local_id < function->local_begin) {\n        return false;\n    }\n    local_index = local_id - function->local_begin;\n    if (local_index >= layout->local_count ||\n        (layout->local_count != 0U && layout->local_offsets == NULL)) {\n        return false;\n    }\n    *offset = layout->local_offsets[local_index];\n    return true;\n}\n\nbool minic_riscv64_layout_function(const char *path,\n                                   const MinicC0Program *program,\n                                   const MinicFunction *function,\n                                   MinicRiscv64FunctionLayout *layout,\n                                   MinicDiagnostic *diagnostic) {\n    MinicRiscv64FunctionLayout result;\n    size_t local_index;\n    size_t storage_size;\n\n    if (program == NULL || function == NULL || layout == NULL) {\n        minic_riscv64_layout_error(diagnostic, path, \"function layout inputs are invalid\");\n        return false;\n    }\n    minic_riscv64_function_layout_initialize(&result);\n    if (!function->is_defined) {\n        *layout = result;\n        return true;\n    }\n    if (function->local_begin > program->local_count ||\n        function->local_count > program->local_count - function->local_begin) {\n        minic_riscv64_layout_error(diagnostic, path, \"function local range is invalid\");\n        return false;\n    }\n\n    result.local_count = function->local_count;\n    if (result.local_count != 0U) {\n        result.local_offsets = (size_t *)calloc(result.local_count, sizeof(*result.local_offsets));\n        if (result.local_offsets == NULL) {\n            minic_riscv64_layout_error(\n                diagnostic, path, \"out of memory while laying out RV64 function\");\n            return false;\n        }\n    }\n\n    storage_size = 0U;\n    for (local_index = 0U; local_index < function->local_count; ++local_index) {\n        const MinicLocal *local;\n        size_t element_size;\n        size_t object_size;\n        size_t object_alignment;\n        size_t object_offset;\n\n        local = &program->locals[function->local_begin + local_index];\n        if (!minic_riscv64_type_layout(program, local->type, &element_size, &object_alignment) ||\n            local->element_count == 0U || element_size > SIZE_MAX / local->element_count) {\n            minic_riscv64_layout_error(\n                diagnostic, path, \"local object size is invalid for the RV64 target\");\n            minic_riscv64_function_layout_destroy(&result);\n            return false;\n        }\n        object_size = element_size * local->element_count;\n        if (!minic_riscv64_align_up(storage_size, object_alignment, &object_offset) ||\n            object_offset > SIZE_MAX - object_size) {\n            minic_riscv64_layout_error(\n                diagnostic, path, \"local object layout exceeds the RV64 target range\");\n            minic_riscv64_function_layout_destroy(&result);\n            return false;\n        }\n        result.local_offsets[local_index] = object_offset;\n        storage_size = object_offset + object_size;\n    }\n    result.local_storage_size = storage_size;\n    *layout = result;\n    return true;\n}\n\n"""
text = replace_once(text, anchor, insert + anchor, "layout.c function layout insertion")
old_loop = """    for (function_index = 0U; function_index < program->function_count; ++function_index) {\n        MinicFunction *function;\n        size_t local_index;\n        size_t storage_size;\n\n        function = &program->functions[function_index];\n        if (!function->is_defined) {\n            function->local_storage_size = 0U;\n            continue;\n        }\n        if (function->local_begin > program->local_count ||\n            function->local_count > program->local_count - function->local_begin) {\n            minic_riscv64_layout_error(diagnostic, path, \"function local range is invalid\");\n            return false;\n        }\n\n        storage_size = 0U;\n        for (local_index = 0U; local_index < function->local_count; ++local_index) {\n            MinicLocal *local;\n            size_t element_size;\n            size_t object_size;\n            size_t object_alignment;\n            size_t object_offset;\n\n            local = &program->locals[function->local_begin + local_index];\n            if (!minic_riscv64_type_layout(\n                    program, local->type, &element_size, &object_alignment) ||\n                local->element_count == 0U || element_size > SIZE_MAX / local->element_count) {\n                minic_riscv64_layout_error(\n                    diagnostic, path, \"local object size is invalid for the RV64 target\");\n                return false;\n            }\n            object_size = element_size * local->element_count;\n            if (!minic_riscv64_align_up(storage_size, object_alignment, &object_offset) ||\n                object_offset > SIZE_MAX - object_size) {\n                minic_riscv64_layout_error(\n                    diagnostic, path, \"local object layout exceeds the RV64 target range\");\n                return false;\n            }\n            local->storage_offset = object_offset;\n            storage_size = object_offset + object_size;\n        }\n        function->local_storage_size = storage_size;\n    }\n"""
new_loop = """    for (function_index = 0U; function_index < program->function_count; ++function_index) {\n        MinicRiscv64FunctionLayout function_layout;\n        MinicFunction *function;\n        size_t local_index;\n\n        function = &program->functions[function_index];\n        minic_riscv64_function_layout_initialize(&function_layout);\n        if (!minic_riscv64_layout_function(\n                path, program, function, &function_layout, diagnostic)) {\n            minic_riscv64_function_layout_destroy(&function_layout);\n            return false;\n        }\n        for (local_index = 0U; local_index < function_layout.local_count; ++local_index) {\n            program->locals[function->local_begin + local_index].storage_offset =\n                function_layout.local_offsets[local_index];\n        }\n        function->local_storage_size = function_layout.local_storage_size;\n        minic_riscv64_function_layout_destroy(&function_layout);\n    }\n"""
text = replace_once(text, old_loop, new_loop, "layout.c compatibility adapter")
write(path, text)

# codegen_internal.h: FrameLayout now consumes the canonical function-local placement result.
path = "src/target/riscv64/codegen_internal.h"
text = read(path)
old = """bool minic_riscv64_frame_layout(const MinicC0Program *program,\n                                const MinicFunction *function,\n                                MinicRiscv64FrameLayout *layout);\n"""
new = """bool minic_riscv64_frame_layout(const MinicC0Program *program,\n                                const MinicFunction *function,\n                                const MinicRiscv64FunctionLayout *function_layout,\n                                MinicRiscv64FrameLayout *layout);\n"""
text = replace_once(text, old, new, "frame layout declaration")
write(path, text)

# codegen_support.c: remove frame sizing's dependency on AST-cached local storage size.
path = "src/target/riscv64/codegen_support.c"
text = read(path)
pattern = re.compile(r"bool minic_riscv64_frame_layout\(const MinicC0Program \*program,\n                                const MinicFunction \*function,\n                                MinicRiscv64FrameLayout \*layout\) \{.*?\n\}", re.S)
match = pattern.search(text)
if match is None:
    raise SystemExit("codegen_support.c: frame layout function anchor missing")
block = match.group(0)
block = block.replace(
    "const MinicFunction *function,\n                                MinicRiscv64FrameLayout *layout)",
    "const MinicFunction *function,\n                                const MinicRiscv64FunctionLayout *function_layout,\n                                MinicRiscv64FrameLayout *layout)",
    1,
)
block = block.replace(
    "if (program == NULL || function == NULL || layout == NULL ||",
    "if (program == NULL || function == NULL || function_layout == NULL || layout == NULL ||",
    1,
)
block = block.replace("function->local_storage_size", "function_layout->local_storage_size")
text = text[: match.start()] + block + text[match.end() :]
write(path, text)

# codegen_function.c: construct the per-function side state once and use it for frame placement.
path = "src/target/riscv64/codegen_function.c"
text = read(path)
old = """static bool minic_riscv64_emit_function(FILE *file,\n                                        const MinicC0Program *program,\n                                        const MinicFunction *function,\n                                        size_t *label_counter) {\n    MinicRiscv64FrameLayout frame_layout;\n    size_t frame_size;\n    bool success;\n    const char *symbol_name;\n\n    if (function == NULL || !function->is_defined || function->name_length == 0U ||\n        function->body_block >= program->block_count ||\n        !minic_riscv64_frame_layout(program, function, &frame_layout)) {\n        return false;\n    }\n    frame_size = frame_layout.frame_size;\n    symbol_name = minic_c0_function_symbol_name(function);\n    if (symbol_name == NULL || symbol_name[0] == '\\0') {\n        return false;\n    }\n"""
new = """static bool minic_riscv64_emit_function(FILE *file,\n                                        const MinicC0Program *program,\n                                        const MinicFunction *function,\n                                        size_t *label_counter) {\n    MinicRiscv64FunctionLayout function_layout;\n    MinicRiscv64FrameLayout frame_layout;\n    size_t frame_size;\n    bool success;\n    const char *symbol_name;\n\n    minic_riscv64_function_layout_initialize(&function_layout);\n    if (function == NULL || !function->is_defined || function->name_length == 0U ||\n        function->body_block >= program->block_count ||\n        !minic_riscv64_layout_function(NULL, program, function, &function_layout, NULL) ||\n        !minic_riscv64_frame_layout(program, function, &function_layout, &frame_layout)) {\n        minic_riscv64_function_layout_destroy(&function_layout);\n        return false;\n    }\n    frame_size = frame_layout.frame_size;\n    symbol_name = minic_c0_function_symbol_name(function);\n    if (symbol_name == NULL || symbol_name[0] == '\\0') {\n        minic_riscv64_function_layout_destroy(&function_layout);\n        return false;\n    }\n"""
text = replace_once(text, old, new, "codegen function prologue")
old = """    if (success) {\n        success = fprintf(file,\n                          \"  ret\\n\"\n                          \".size %s, .-%s\\n\",\n                          symbol_name,\n                          symbol_name) >= 0;\n    }\n    return success;\n}\n\nbool minic_riscv64_write_c0_program"""
new = """    if (success) {\n        success = fprintf(file,\n                          \"  ret\\n\"\n                          \".size %s, .-%s\\n\",\n                          symbol_name,\n                          symbol_name) >= 0;\n    }\n    minic_riscv64_function_layout_destroy(&function_layout);\n    return success;\n}\n\nbool minic_riscv64_write_c0_program"""
text = replace_once(text, old, new, "codegen function epilogue")
write(path, text)

# layout unit test: assert the side-state result directly, while keeping the old mirror as a transition gate.
path = "tests/target/riscv64/layout_test.c"
text = read(path)
anchor = """    function = minic_c0_program_function(&program, function_id);\n    if (function == NULL || function->local_storage_size != 76U ||\n"""
insert = """    function = minic_c0_program_function(&program, function_id);\n    {\n        MinicRiscv64FunctionLayout function_layout;\n        size_t expected_offsets[8] = {0U, 1U, 4U, 8U, 24U, 32U, 48U, 52U};\n        size_t index;\n\n        minic_riscv64_function_layout_initialize(&function_layout);\n        if (function == NULL ||\n            !minic_riscv64_layout_function(\n                \"layout-function\", &program, function, &function_layout, &diagnostic) ||\n            function_layout.local_count != 8U || function_layout.local_storage_size != 76U) {\n            minic_riscv64_function_layout_destroy(&function_layout);\n            minic_c0_program_destroy(&program);\n            return fail(\"function side-state layout\");\n        }\n        for (index = 0U; index < 8U; ++index) {\n            size_t offset;\n\n            if (!minic_riscv64_function_layout_local_offset(\n                    &function_layout, function, index, &offset) ||\n                offset != expected_offsets[index]) {\n                minic_riscv64_function_layout_destroy(&function_layout);\n                minic_c0_program_destroy(&program);\n                return fail(\"function side-state local offsets\");\n            }\n        }\n        minic_riscv64_function_layout_destroy(&function_layout);\n    }\n    if (function == NULL || function->local_storage_size != 76U ||\n"""
text = replace_once(text, anchor, insert, "layout test side-state assertion")
write(path, text)

print("MATERIALIZED rv64-function-layout-v1")
