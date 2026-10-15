#!/usr/bin/env python3
"""Materialize generic RV64 function-emission stage diagnostics."""
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, found {count}")
    return text.replace(old, new, 1)


path = Path("src/target/riscv64/codegen_function.c")
text = path.read_text()
marker = "const char **failure_stage"
if marker not in text:
    text = replace_once(
        text,
        '''static bool minic_riscv64_emit_function(FILE *file,\n                                        const MinicC0Program *program,\n                                        const MinicFunction *function,\n                                        size_t *label_counter) {\n''',
        '''static bool minic_riscv64_emit_function(FILE *file,\n                                        const MinicC0Program *program,\n                                        const MinicFunction *function,\n                                        size_t *label_counter,\n                                        const char **failure_stage) {\n''',
        "function emitter signature",
    )
    text = replace_once(
        text,
        '''    if (function == NULL || !function->is_defined || function->name_length == 0U ||\n        function->body_block >= program->block_count) {\n        return false;\n    }\n    minic_riscv64_function_layout_initialize(&function_layout);\n    if (!minic_riscv64_layout_function(NULL, program, function, &function_layout, NULL)) {\n        return false;\n    }\n''',
        '''    if (failure_stage != NULL) {\n        *failure_stage = "validation";\n    }\n    if (function == NULL || !function->is_defined || function->name_length == 0U ||\n        function->body_block >= program->block_count) {\n        return false;\n    }\n    minic_riscv64_function_layout_initialize(&function_layout);\n    if (failure_stage != NULL) {\n        *failure_stage = "layout";\n    }\n    if (!minic_riscv64_layout_function(NULL, program, function, &function_layout, NULL)) {\n        return false;\n    }\n''',
        "function emitter layout stage",
    )
    text = replace_once(
        text,
        '''    if (!minic_riscv64_frame_layout_from_function_layout(\n            program, function, &function_layout, &frame_layout)) {\n''',
        '''    if (failure_stage != NULL) {\n        *failure_stage = "frame-layout";\n    }\n    if (!minic_riscv64_frame_layout_from_function_layout(\n            program, function, &function_layout, &frame_layout)) {\n''',
        "function emitter frame stage",
    )
    text = replace_once(
        text,
        '''    frame_size = frame_layout.frame_size;\n    if (!minic_riscv64_function_symbol_from_function(function, &symbol)) {\n''',
        '''    frame_size = frame_layout.frame_size;\n    if (failure_stage != NULL) {\n        *failure_stage = "symbol";\n    }\n    if (!minic_riscv64_function_symbol_from_function(function, &symbol)) {\n''',
        "function emitter symbol stage",
    )
    text = replace_once(
        text,
        '''    success = minic_riscv64_emit_function_symbol_begin(file, &symbol);\n''',
        '''    if (failure_stage != NULL) {\n        *failure_stage = "prologue";\n    }\n    success = minic_riscv64_emit_function_symbol_begin(file, &symbol);\n''',
        "function emitter prologue stage",
    )
    text = replace_once(
        text,
        '''    if (success) {\n        MinicRiscv64AbiCursor abi_cursor;\n''',
        '''    if (success) {\n        MinicRiscv64AbiCursor abi_cursor;\n''',
        "function emitter ABI anchor",
    )
    # Stage assignment immediately before ABI/parameter placement.
    abi_anchor = '''    if (success) {\n        MinicRiscv64AbiCursor abi_cursor;\n        MinicRiscv64AbiValue return_value;\n        size_t parameter_index;\n\n'''
    abi_new = abi_anchor + '''        if (failure_stage != NULL) {\n            *failure_stage = "abi-parameters";\n        }\n'''
    text = replace_once(text, abi_anchor, abi_new, "function emitter ABI stage")
    text = replace_once(
        text,
        '''    if (success) {\n        success = minic_riscv64_emit_block(\n            file, program, function, &function_layout, function->body_block, label_counter);\n    }\n''',
        '''    if (success) {\n        if (failure_stage != NULL) {\n            *failure_stage = "body";\n        }\n        success = minic_riscv64_emit_block(\n            file, program, function, &function_layout, function->body_block, label_counter);\n    }\n''',
        "function emitter body stage",
    )
    text = replace_once(
        text,
        '''    if (success) {\n        success = fprintf(file,\n                          "  li a0, 0\\n"\n                          ".L%s_return:\\n",\n                          function->name) >= 0;\n    }\n''',
        '''    if (success) {\n        if (failure_stage != NULL) {\n            *failure_stage = "epilogue";\n        }\n        success = fprintf(file,\n                          "  li a0, 0\\n"\n                          ".L%s_return:\\n",\n                          function->name) >= 0;\n    }\n''',
        "function emitter epilogue stage",
    )
    text = replace_once(
        text,
        '''    minic_riscv64_function_layout_destroy(&function_layout);\n    return success;\n}\n''',
        '''    minic_riscv64_function_layout_destroy(&function_layout);\n    if (success && failure_stage != NULL) {\n        *failure_stage = NULL;\n    }\n    return success;\n}\n''',
        "function emitter success stage",
    )
    text = replace_once(
        text,
        '''        } else {\n            success = minic_riscv64_emit_function(file, program, function, &label_counter);\n        }\n        if (!success && diagnostic != NULL && diagnostic->message[0] == '\\0') {\n            char message[256];\n            const char *symbol_name;\n\n            symbol_name = minic_c0_function_symbol_name(function);\n            (void)snprintf(message,\n                           sizeof(message),\n                           "cannot emit RISC-V function '%s' (index=%zu)",\n                           symbol_name != NULL ? symbol_name : "<unnamed>",\n                           function_index);\n''',
        '''        } else {\n            const char *failure_stage;\n\n            failure_stage = NULL;\n            success = minic_riscv64_emit_function(\n                file, program, function, &label_counter, &failure_stage);\n            if (!success && diagnostic != NULL && diagnostic->message[0] == '\\0') {\n                char message[256];\n                const char *symbol_name;\n\n                symbol_name = minic_c0_function_symbol_name(function);\n                (void)snprintf(message,\n                               sizeof(message),\n                               "cannot emit RISC-V function '%s' (index=%zu stage=%s)",\n                               symbol_name != NULL ? symbol_name : "<unnamed>",\n                               function_index,\n                               failure_stage != NULL ? failure_stage : "unknown");\n                minic_riscv64_set_diagnostic(diagnostic, path, message);\n            }\n        }\n        if (!success && diagnostic != NULL && diagnostic->message[0] == '\\0') {\n            char message[256];\n            const char *symbol_name;\n\n            symbol_name = minic_c0_function_symbol_name(function);\n            (void)snprintf(message,\n                           sizeof(message),\n                           "cannot emit RISC-V function '%s' (index=%zu)",\n                           symbol_name != NULL ? symbol_name : "<unnamed>",\n                           function_index);\n''',
        "function emitter top-level diagnostic",
    )
    path.write_text(text)
