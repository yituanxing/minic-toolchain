#!/usr/bin/env python3
from pathlib import Path

path = Path("src/target/riscv64/codegen_expression.c")
text = path.read_text()
old = r'''                if (!minic_type_is_record(argument->type) ||
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
                    fprintf(file, "  mv t0, a0\n") < 0 ||
                    !minic_riscv64_emit_integer_aggregate_load_chunk(
                        file, program, abi_parameter_types[argument_index], 0U, "t1", "t0") ||
                    fprintf(file, "  sd t1, 0(sp)\n") < 0 ||
                    (aggregate_chunks == 2U &&
                     (!minic_riscv64_emit_integer_aggregate_load_chunk(
                          file, program, abi_parameter_types[argument_index], 1U, "t1", "t0") ||
                      fprintf(file, "  sd t1, 8(sp)\n") < 0))) {
                    return false;
                }
'''
new = r'''                if (!minic_type_is_record(argument->type) ||
                    argument->type.record_id != abi_parameter_types[argument_index].record_id ||
                    !minic_riscv64_integer_aggregate_abi(program,
                                                         abi_parameter_types[argument_index],
                                                         &aggregate_size,
                                                         &aggregate_chunks)) {
                    return false;
                }
                if (argument->value_category == MINIC_VALUE_LVALUE) {
                    if (!minic_riscv64_emit_lvalue_address(
                            file,
                            program,
                            function,
                            expression->value.call.arguments[argument_index]) ||
                        !minic_riscv64_emit_stack_allocate(file, 16U) ||
                        fprintf(file, "  mv t0, a0\n") < 0 ||
                        !minic_riscv64_emit_integer_aggregate_load_chunk(
                            file, program, abi_parameter_types[argument_index], 0U, "t1", "t0") ||
                        fprintf(file, "  sd t1, 0(sp)\n") < 0 ||
                        (aggregate_chunks == 2U &&
                         (!minic_riscv64_emit_integer_aggregate_load_chunk(
                              file,
                              program,
                              abi_parameter_types[argument_index],
                              1U,
                              "t1",
                              "t0") ||
                          fprintf(file, "  sd t1, 8(sp)\n") < 0))) {
                        return false;
                    }
                } else if (argument->kind == MINIC_EXPRESSION_CALL) {
                    if (!minic_riscv64_emit_expression(
                            file,
                            program,
                            function,
                            expression->value.call.arguments[argument_index]) ||
                        !minic_riscv64_emit_stack_allocate(file, 16U) ||
                        fprintf(file, "  sd a0, 0(sp)\n") < 0 ||
                        (aggregate_chunks == 2U && fprintf(file, "  sd a1, 8(sp)\n") < 0)) {
                        return false;
                    }
                } else {
                    return false;
                }
'''
if old not in text:
    if new in text:
        raise SystemExit(0)
    raise SystemExit("aggregate rvalue argument anchor not found")
path.write_text(text.replace(old, new, 1))
