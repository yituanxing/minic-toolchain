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
                    fprintf(file,
                            "  mv t0, a0\n"
                            "  ld t1, 0(t0)\n"
                            "  sd t1, 0(sp)\n") < 0 ||
                    (aggregate_chunks == 2U &&
                     fprintf(file, "  ld t1, 8(t0)\n  sd t1, 8(sp)\n") < 0)) {
                    return false;
                }
'''
new = r'''                if (!minic_type_is_record(argument->type) ||
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
                    fprintf(file,
                            "  mv t0, a0\n"
                            "  ld t1, 0(t0)\n"
                            "  sd t1, 0(sp)\n") < 0 ||
                    (aggregate_chunks == 2U &&
                     fprintf(file, "  ld t1, 8(t0)\n  sd t1, 8(sp)\n") < 0)) {
                    fprintf(stderr,
                            "CODEGEN_CALL_ARG aggregate caller=%s callee=%s arg=%zu expr=%zu kind=%d type=%d/%u record=%zu vcat=%d abi_record=%zu\n",
                            function != NULL ? function->name : "<null>",
                            direct_callee != NULL ? direct_callee->name : "<indirect>",
                            argument_index,
                            (size_t)expression->value.call.arguments[argument_index],
                            (int)argument->kind,
                            (int)argument->type.base_kind,
                            argument->type.pointer_depth,
                            (size_t)argument->type.record_id,
                            (int)argument->value_category,
                            (size_t)abi_parameter_types[argument_index].record_id);
                    return false;
                }
'''
if old in text:
    text = text.replace(old, new, 1)
elif new not in text:
    raise SystemExit("aggregate call argument trace anchor not found")

old = r'''            if (!minic_riscv64_emit_expression(
                    file, program, function, expression->value.call.arguments[argument_index])) {
                return false;
            }
'''
new = r'''            if (!minic_riscv64_emit_expression(
                    file, program, function, expression->value.call.arguments[argument_index])) {
                fprintf(stderr,
                        "CODEGEN_CALL_ARG scalar caller=%s callee=%s arg=%zu expr=%zu kind=%d type=%d/%u vcat=%d fixed=%d abi_type=%d/%u\n",
                        function != NULL ? function->name : "<null>",
                        direct_callee != NULL ? direct_callee->name : "<indirect>",
                        argument_index,
                        (size_t)expression->value.call.arguments[argument_index],
                        (int)argument->kind,
                        (int)argument->type.base_kind,
                        argument->type.pointer_depth,
                        (int)argument->value_category,
                        argument_index < parameter_count ? 1 : 0,
                        argument_index < parameter_count ? (int)abi_parameter_types[argument_index].base_kind : -1,
                        argument_index < parameter_count ? abi_parameter_types[argument_index].pointer_depth : 0U);
                return false;
            }
'''
if old in text:
    text = text.replace(old, new, 1)
elif new not in text:
    raise SystemExit("scalar call argument trace anchor not found")

path.write_text(text)
