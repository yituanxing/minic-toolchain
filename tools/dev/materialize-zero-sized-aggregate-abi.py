#!/usr/bin/env python3
from pathlib import Path

support = Path("src/target/riscv64/codegen_support.c")
text = support.read_text()
old = '''        !minic_riscv64_integer_aggregate_member_type(program, type) ||
        !minic_riscv64_type_layout(program, type, &size, &alignment) || size == 0U || size > 16U) {
'''
new = '''        !minic_riscv64_integer_aggregate_member_type(program, type) ||
        !minic_riscv64_type_layout(program, type, &size, &alignment) || size > 16U) {
'''
if old in text:
    text = text.replace(old, new, 1)
elif new not in text:
    raise SystemExit("zero-sized aggregate ABI anchor not found")
support.write_text(text)

expr = Path("src/target/riscv64/codegen_expression.c")
text = expr.read_text()
old = '''                if (!minic_type_is_record(argument->type) ||
                    argument->type.record_id != abi_parameter_types[argument_index].record_id ||
                    !minic_riscv64_integer_aggregate_abi(program,
                                                         abi_parameter_types[argument_index],
                                                         &aggregate_size,
                                                         &aggregate_chunks) ||
                    !minic_c0_record_value_is_copy_source(
                        program, expression->value.call.arguments[argument_index]) ||
                    !minic_riscv64_emit_record_value_temporary(
                        file,
                        program,
                        function,
                        expression->value.call.arguments[argument_index],
                        aggregate_size,
                        16U)) {
                    return false;
                }
'''
new = '''                if (!minic_type_is_record(argument->type) ||
                    argument->type.record_id != abi_parameter_types[argument_index].record_id ||
                    !minic_riscv64_integer_aggregate_abi(program,
                                                         abi_parameter_types[argument_index],
                                                         &aggregate_size,
                                                         &aggregate_chunks)) {
                    return false;
                }
                if (aggregate_chunks == 0U) {
                    continue;
                }
                if (!minic_c0_record_value_is_copy_source(
                        program, expression->value.call.arguments[argument_index]) ||
                    !minic_riscv64_emit_record_value_temporary(
                        file,
                        program,
                        function,
                        expression->value.call.arguments[argument_index],
                        aggregate_size,
                        16U)) {
                    return false;
                }
'''
if old in text:
    text = text.replace(old, new, 1)
elif new not in text:
    raise SystemExit("zero-sized aggregate call anchor not found")
expr.write_text(text)
