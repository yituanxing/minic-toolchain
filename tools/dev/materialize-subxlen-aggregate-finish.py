#!/usr/bin/env python3
from pathlib import Path


def replace_region(path: str, begin: str, end: str, replacement: str, label: str) -> None:
    file = Path(path)
    text = file.read_text()
    start = text.find(begin)
    if start < 0:
        if replacement in text:
            return
        raise SystemExit(f"missing {label} begin")
    stop = text.find(end, start)
    if stop < 0:
        raise SystemExit(f"missing {label} end")
    stop += len(end)
    file.write_text(text[:start] + replacement + text[stop:])


call_begin = """                if (!minic_type_is_record(argument->type) ||
"""
call_end = """                    return false;
                }
"""
call_new = """                if (!minic_type_is_record(argument->type) ||
                    argument->type.record_id != abi_parameter_types[argument_index].record_id ||
                    argument->value_category != MINIC_VALUE_LVALUE ||
                    !minic_riscv64_integer_aggregate_abi(program,
                                                         abi_parameter_types[argument_index],
                                                         &aggregate_size,
                                                         &aggregate_chunks) ||
                    !minic_riscv64_emit_lvalue_address(
                        file,
                        program,
                        function,
                        expression->value.call.arguments[argument_index]) ||
                    !minic_riscv64_emit_stack_allocate(file, 16U) ||
                    fprintf(file, "  mv t0, a0\\n") < 0 ||
                    !minic_riscv64_emit_integer_aggregate_load_chunk(
                        file, program, abi_parameter_types[argument_index], 0U, "t1", "t0") ||
                    fprintf(file, "  sd t1, 0(sp)\\n") < 0 ||
                    (aggregate_chunks == 2U &&
                     (!minic_riscv64_emit_integer_aggregate_load_chunk(
                          file, program, abi_parameter_types[argument_index], 1U, "t1", "t0") ||
                      fprintf(file, "  sd t1, 8(sp)\\n") < 0))) {
                    return false;
                }
"""
replace_region(
    "src/target/riscv64/codegen_expression.c", call_begin, call_end, call_new, "aggregate call staging"
)

return_begin = """            if (value->value_category == MINIC_VALUE_LVALUE) {
"""
return_end = """                    return false;
                }
"""
return_new = """            if (value->value_category == MINIC_VALUE_LVALUE) {
                if (!minic_riscv64_emit_lvalue_address(
                        file, program, function, statement->expression) ||
                    fprintf(file, "  mv t0, a0\\n") < 0 ||
                    !minic_riscv64_emit_integer_aggregate_load_chunk(
                        file, program, function->return_type, 0U, "a0", "t0") ||
                    (aggregate_chunks == 2U &&
                     !minic_riscv64_emit_integer_aggregate_load_chunk(
                         file, program, function->return_type, 1U, "a1", "t0"))) {
                    return false;
                }
"""
replace_region(
    "src/target/riscv64/codegen_statement.c",
    return_begin,
    return_end,
    return_new,
    "aggregate return load",
)
