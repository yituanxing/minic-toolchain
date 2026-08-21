#!/usr/bin/env python3
"""Materialize RV64 indirect variadic call ABI support once."""
from pathlib import Path


def replace_once(path: Path, before: str, after: str) -> None:
    text = path.read_text()
    if after in text:
        return
    count = text.count(before)
    if count != 1:
        raise SystemExit(f"{path}: expected one materialization anchor, found {count}")
    path.write_text(text.replace(before, after, 1))


path = Path("src/target/riscv64/codegen_expression.c")
replace_once(
    path,
    """            indirect_type = minic_c0_program_function_type(program, function_type.function_type_id);
            if (indirect_type == NULL ||
                indirect_type->parameter_count > MINIC_MAX_FUNCTION_PARAMETERS ||
                argument_count != indirect_type->parameter_count ||
                !minic_type_equal(expression->type, indirect_type->return_type) ||
                !minic_riscv64_emit_expression(
                    file, program, function, function_layout, expression->value.call.callee) ||
                fprintf(file, "  addi sp, sp, -16\\n  sd a0, 0(sp)\\n") < 0) {
                return false;
            }
            staged_bytes = 16U;
            parameter_types = indirect_type->parameter_types;
            parameter_count = indirect_type->parameter_count;
""",
    """            indirect_type = minic_c0_program_function_type(program, function_type.function_type_id);
            if (indirect_type == NULL ||
                indirect_type->parameter_count > MINIC_MAX_FUNCTION_PARAMETERS ||
                argument_count > MINIC_MAX_FUNCTION_PARAMETERS ||
                !minic_type_equal(expression->type, indirect_type->return_type)) {
                return false;
            }
            parameter_types = indirect_type->parameter_types;
            parameter_count = indirect_type->parameter_count;
            is_variadic = indirect_type->is_variadic;
            if (argument_count < parameter_count ||
                (!is_variadic && argument_count != parameter_count) ||
                !minic_riscv64_emit_expression(
                    file, program, function, function_layout, expression->value.call.callee) ||
                fprintf(file, "  addi sp, sp, -16\\n  sd a0, 0(sp)\\n") < 0) {
                return false;
            }
            staged_bytes = 16U;
""",
)
replace_once(
    path,
    """        use_formal_location_path = true;
        for (argument_index = 0U; argument_index < argument_count; ++argument_index) {
            const MinicRiscv64AbiValue *value;

            value = &abi_values[argument_index];
""",
    """        use_formal_location_path = argument_count == parameter_count;
        for (argument_index = 0U; use_formal_location_path && argument_index < argument_count;
             ++argument_index) {
            const MinicRiscv64AbiValue *value;

            value = &abi_values[argument_index];
""",
)
