#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}: {old[:100]!r}")
    target.write_text(text.replace(old, new, 1))


def replace_tail_function(path: str, marker: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    start = text.find(marker)
    if start < 0 or text.find(marker, start + 1) >= 0:
        raise SystemExit(f"{path}: expected one function marker: {marker!r}")
    # frame_size is the final function in codegen_support.c.  Anchor on the function
    # boundary instead of its body so earlier discovery edits can safely evolve its
    # overflow checks without making this staged transform fuzzy.
    target.write_text(text[:start] + new)


replace_once(
    "src/target/riscv64/codegen_internal.h",
    "bool minic_riscv64_frame_size(const MinicFunction *function, size_t *frame_size);\n",
    """typedef struct MinicRiscv64FrameLayout {
    size_t frame_size;
    size_t saved_ra_offset;
    size_t saved_s0_offset;
    size_t varargs_offset;
    size_t varargs_size;
    size_t integer_parameter_count;
} MinicRiscv64FrameLayout;

bool minic_riscv64_frame_layout(const MinicC0Program *program,
                                const MinicFunction *function,
                                MinicRiscv64FrameLayout *layout);
""",
)

replace_tail_function(
    "src/target/riscv64/codegen_support.c",
    "bool minic_riscv64_frame_size(",
    """bool minic_riscv64_frame_layout(const MinicC0Program *program,
                                const MinicFunction *function,
                                MinicRiscv64FrameLayout *layout) {
    size_t integer_parameter_count;
    size_t parameter_index;
    size_t required_bytes;
    size_t varargs_size;

    if (program == NULL || function == NULL || layout == NULL || function->parameter_count > 8U) {
        return false;
    }

    integer_parameter_count = 0U;
    for (parameter_index = 0U; parameter_index < function->parameter_count; ++parameter_index) {
        const MinicLocal *parameter;

        parameter = minic_c0_program_local(program, function->local_begin + parameter_index);
        if (parameter == NULL) {
            return false;
        }
        if (minic_type_is_double(parameter->type) || minic_type_is_float(parameter->type)) {
            continue;
        }
        if (!minic_type_is_integer(parameter->type) && !minic_type_is_pointer(parameter->type)) {
            return false;
        }
        integer_parameter_count += 1U;
    }
    if (integer_parameter_count > 8U) {
        return false;
    }

    varargs_size = function->is_variadic ? (8U - integer_parameter_count) * 8U : 0U;
    if (function->local_storage_size > SIZE_MAX - 16U ||
        function->local_storage_size + 16U > SIZE_MAX - varargs_size) {
        return false;
    }
    required_bytes = function->local_storage_size + 16U + varargs_size;
    if (required_bytes > SIZE_MAX - 15U) {
        return false;
    }

    layout->frame_size = (required_bytes + 15U) & ~(size_t)15U;
    layout->varargs_size = varargs_size;
    layout->varargs_offset = layout->frame_size - varargs_size;
    if (layout->varargs_offset < 16U ||
        function->local_storage_size > layout->varargs_offset - 16U) {
        return false;
    }
    layout->saved_ra_offset = layout->varargs_offset - 8U;
    layout->saved_s0_offset = layout->varargs_offset - 16U;
    layout->integer_parameter_count = integer_parameter_count;
    return true;
}
""",
)

replace_once(
    "src/target/riscv64/codegen_function.c",
    """    size_t frame_size;
    bool success;

    if (function == NULL || !function->is_defined || function->name_length == 0U ||
        function->body_block >= program->block_count ||
        !minic_riscv64_frame_size(function, &frame_size)) {
        return false;
    }
""",
    """    MinicRiscv64FrameLayout frame_layout;
    size_t frame_size;
    bool success;

    if (function == NULL || !function->is_defined || function->name_length == 0U ||
        function->body_block >= program->block_count ||
        !minic_riscv64_frame_layout(program, function, &frame_layout)) {
        return false;
    }
    frame_size = frame_layout.frame_size;
""",
)

replace_once(
    "src/target/riscv64/codegen_function.c",
    """        success = minic_riscv64_emit_sp_store64(file, "ra", frame_size - 8U) &&
                  minic_riscv64_emit_sp_store64(file, "s0", frame_size - 16U) &&
                  fprintf(file, "  mv s0, sp\\n") >= 0;
    }
    if (success && function->parameter_count > 8U) {
""",
    """        success = minic_riscv64_emit_sp_store64(file, "ra", frame_layout.saved_ra_offset) &&
                  minic_riscv64_emit_sp_store64(file, "s0", frame_layout.saved_s0_offset) &&
                  fprintf(file, "  mv s0, sp\\n") >= 0;
    }
    if (success && function->is_variadic) {
        size_t register_index;

        for (register_index = frame_layout.integer_parameter_count;
             success && register_index < 8U;
             ++register_index) {
            size_t offset;

            offset = frame_layout.varargs_offset +
                     (register_index - frame_layout.integer_parameter_count) * 8U;
            success = minic_riscv64_emit_sp_store64(
                file, minic_riscv64_argument_registers[register_index], offset);
        }
    }
    if (success && function->parameter_count > 8U) {
""",
)

replace_once(
    "src/target/riscv64/codegen_function.c",
    """        success = minic_riscv64_emit_sp_load64(file, "ra", frame_size - 8U) &&
                  minic_riscv64_emit_sp_load64(file, "s0", frame_size - 16U);
""",
    """        success = minic_riscv64_emit_sp_load64(file, "ra", frame_layout.saved_ra_offset) &&
                  minic_riscv64_emit_sp_load64(file, "s0", frame_layout.saved_s0_offset);
""",
)

replace_once(
    "src/target/riscv64/codegen_expression.c",
    "#include <inttypes.h>\n",
    "#include <inttypes.h>\n#include <string.h>\n",
)

replace_once(
    "src/target/riscv64/codegen_expression.c",
    """        for (argument_index = 0U; argument_index < argument_count; ++argument_index) {
            const MinicExpression *argument;
""",
    """        if (!is_indirect && direct_callee != NULL && direct_callee->name_length == 16U &&
            strcmp(direct_callee->name, "__minic_va_start") == 0) {
            MinicRiscv64FrameLayout frame_layout;

            if (argument_count != 0U || !function->is_variadic ||
                !minic_type_is_pointer(expression->type) ||
                !minic_riscv64_frame_layout(program, function, &frame_layout)) {
                return false;
            }
            if (frame_layout.varargs_offset <= 2047U) {
                return fprintf(file, "  addi a0, s0, %zu\\n", frame_layout.varargs_offset) >= 0;
            }
            return fprintf(file,
                           "  li t0, %zu\\n"
                           "  add a0, s0, t0\\n",
                           frame_layout.varargs_offset) >= 0;
        }

        for (argument_index = 0U; argument_index < argument_count; ++argument_index) {
            const MinicExpression *argument;
""",
)

print("staged RV64 variadic register save area and va_start builtin")
