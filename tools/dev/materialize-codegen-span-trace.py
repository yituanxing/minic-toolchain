#!/usr/bin/env python3
from pathlib import Path

statement_path = Path("src/target/riscv64/codegen_statement.c")
text = statement_path.read_text()
old = r'''            fprintf(stderr,
                    "CODEGEN_FAIL statement function=%s block=%zu statement=%zu kind=%d\n",
                    function != NULL ? function->name : "<null>",
                    (size_t)block_id,
                    (size_t)statement_id,
                    statement != NULL ? (int)statement->kind : -1);
'''
new = r'''            fprintf(stderr,
                    "CODEGEN_FAIL statement function=%s block=%zu statement=%zu kind=%d line=%zu column=%zu\n",
                    function != NULL ? function->name : "<null>",
                    (size_t)block_id,
                    (size_t)statement_id,
                    statement != NULL ? (int)statement->kind : -1,
                    statement != NULL ? statement->span.begin.line : 0U,
                    statement != NULL ? statement->span.begin.column : 0U);
'''
if old in text:
    text = text.replace(old, new, 1)
elif new not in text:
    raise SystemExit("statement codegen trace anchor not found")
statement_path.write_text(text)

function_path = Path("src/target/riscv64/codegen_function.c")
text = function_path.read_text()
old = r'''    if (function == NULL || !function->is_defined || function->name_length == 0U ||
        function->body_block >= program->block_count ||
        !minic_riscv64_frame_layout(program, function, &frame_layout)) {
        return false;
    }
'''
new = r'''    if (function == NULL || !function->is_defined || function->name_length == 0U ||
        function->body_block >= program->block_count) {
        fprintf(stderr,
                "CODEGEN_FUNCTION_ENTRY invalid-metadata name=%s\n",
                function == NULL ? "<null>" : function->name);
        return false;
    }
    if (!minic_riscv64_frame_layout(program, function, &frame_layout)) {
        size_t parameter_index;

        fprintf(stderr,
                "CODEGEN_FUNCTION_ENTRY frame-layout name=%s params=%zu locals=%zu..%zu\n",
                function->name,
                function->parameter_count,
                (size_t)function->local_begin,
                (size_t)function->local_end);
        for (parameter_index = 0U; parameter_index < function->parameter_count; ++parameter_index) {
            MinicLocalId local_id;
            const MinicLocal *parameter;

            local_id = function->local_begin + parameter_index;
            parameter = minic_c0_program_local(program, local_id);
            fprintf(stderr,
                    "CODEGEN_FUNCTION_PARAM index=%zu local=%zu kind=%d ptr=%u array=%d array_id=%zu record=%zu\n",
                    parameter_index,
                    (size_t)local_id,
                    parameter == NULL ? -1 : (int)parameter->type.base_kind,
                    parameter == NULL ? 0U : parameter->type.pointer_depth,
                    parameter != NULL && minic_type_is_array(parameter->type) ? 1 : 0,
                    parameter == NULL ? SIZE_MAX : parameter->type.array_type_id,
                    parameter == NULL ? SIZE_MAX : parameter->type.record_id);
        }
        return false;
    }
'''
if old in text:
    text = text.replace(old, new, 1)
elif new not in text:
    raise SystemExit("function entry trace anchor not found")
function_path.write_text(text)
