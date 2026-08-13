#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]
path = root / 'src/target/riscv64/codegen_expression.c'
text = path.read_text()

old = '''            if (argument_index < parameter_count &&
                minic_type_is_record(abi_parameter_types[argument_index])) {
                size_t aggregate_size;
                size_t aggregate_chunks;

                if (!minic_type_is_record(argument->type) ||
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
                    fprintf(file, "  mv t0, a0\\n") < 0) {
                    return false;
                }
                {
                    size_t chunk_index;

                    for (chunk_index = 0U; chunk_index < aggregate_chunks; ++chunk_index) {
                        if (!minic_riscv64_emit_integer_aggregate_chunk_load(
                                file, aggregate_size, chunk_index, "t1", "t0") ||
                            fprintf(file, "  sd t1, %zu(sp)\\n", chunk_index * 8U) < 0) {
                            return false;
                        }
                    }
                }
                continue;
            }
'''
new = '''            if (argument_index < parameter_count &&
                minic_type_is_record(abi_parameter_types[argument_index])) {
                size_t aggregate_size;
                size_t aggregate_chunks;

                if (!minic_type_is_record(argument->type) ||
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
                        fprintf(file, "  mv t0, a0\\n") < 0) {
                        return false;
                    }
                    {
                        size_t chunk_index;

                        for (chunk_index = 0U; chunk_index < aggregate_chunks; ++chunk_index) {
                            if (!minic_riscv64_emit_integer_aggregate_chunk_load(
                                    file, aggregate_size, chunk_index, "t1", "t0") ||
                                fprintf(file, "  sd t1, %zu(sp)\\n", chunk_index * 8U) < 0) {
                                return false;
                            }
                        }
                    }
                } else if (argument->kind == MINIC_EXPRESSION_CALL) {
                    if (!minic_riscv64_emit_expression(
                            file, program, function, expression->value.call.arguments[argument_index]) ||
                        !minic_riscv64_emit_stack_allocate(file, 16U) ||
                        fprintf(file, "  sd a0, 0(sp)\\n") < 0 ||
                        (aggregate_chunks == 2U && fprintf(file, "  sd a1, 8(sp)\\n") < 0)) {
                        return false;
                    }
                } else {
                    return false;
                }
                continue;
            }
'''
if text.count(old) != 1:
    raise SystemExit('fixed aggregate argument staging anchor missing')
path.write_text(text.replace(old, new, 1))

source_path = root / 'tests/compiler/c0/rv64_integer_aggregate_return.c'
source = source_path.read_text()
source += '''\nstruct nested_word {\n    unsigned long value;\n};\n\nstatic struct nested_word nested_identity(struct nested_word value) {\n    return value;\n}\n\nstatic struct nested_word nested_combine(struct nested_word left, struct nested_word right) {\n    struct nested_word result;\n    result.value = left.value | right.value;\n    return result;\n}\n\nstatic struct nested_word nested_call_argument(struct nested_word left, struct nested_word right) {\n    return nested_combine(left, nested_identity(right));\n}\n'''
source_path.write_text(source)

run_path = root / 'tests/compiler/c0/run-rv64-integer-aggregate-return.sh'
run = run_path.read_text()
needle = '''grep -F 'call unwrap_word' "$assembly" >/dev/null\n'''
extra = needle + '''grep -F 'call nested_identity' "$assembly" >/dev/null\ngrep -F 'call nested_combine' "$assembly" >/dev/null\n'''
if run.count(needle) != 1:
    raise SystemExit('aggregate nested-call assertion anchor missing')
run = run.replace(needle, extra, 1)
old_msg = "'PASS compiler/c0/rv64_integer_aggregate_return sizes=4,16 class=integer callee-partial=exact caller-partial=exact return-partial=exact record-call=1'"
new_msg = "'PASS compiler/c0/rv64_integer_aggregate_return sizes=4,8,16 class=integer callee-partial=exact caller-partial=exact nested-call-argument=1 return-partial=exact'"
if run.count(old_msg) != 1:
    raise SystemExit('aggregate focused message anchor missing')
run_path.write_text(run.replace(old_msg, new_msg, 1))
