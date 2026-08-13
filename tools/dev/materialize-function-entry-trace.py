#!/usr/bin/env python3
from pathlib import Path

path = Path("src/target/riscv64/codegen_function.c")
text = path.read_text()
old = '''    if (function == NULL || !function->is_defined || function->name_length == 0U ||
        function->body_block >= program->block_count ||
        !minic_riscv64_frame_layout(program, function, &frame_layout)) {
        return false;
    }
'''
new = '''    if (function == NULL || !function->is_defined || function->name_length == 0U ||
        function->body_block >= program->block_count) {
        fprintf(stderr, "CODEGEN_FUNCTION_ENTRY invalid-metadata name=%s\\n",
                function == NULL ? "<null>" : function->name);
        return false;
    }
    if (!minic_riscv64_frame_layout(program, function, &frame_layout)) {
        size_t parameter_index;

        fprintf(stderr,
                "CODEGEN_FUNCTION_ENTRY frame-layout name=%s params=%zu locals=%zu..%zu\\n",
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
                    "CODEGEN_FUNCTION_PARAM index=%zu local=%zu kind=%d ptr=%u array=%d array_id=%zu record=%zu\\n",
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
if old not in text:
    if new in text:
        raise SystemExit(0)
    raise SystemExit("function entry trace anchor not found")
path.write_text(text.replace(old, new, 1))
