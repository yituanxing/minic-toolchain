#!/usr/bin/env python3
from pathlib import Path
import runpy

makefile = Path("Makefile")
text = makefile.read_text()
old = "\tsrc/target/target_info.c \\\n\tsrc/target/riscv64/layout.c \\\n"
new = "\tsrc/target/target_info.c \\\n\tsrc/target/riscv64/abi.c \\\n\tsrc/target/riscv64/layout.c \\\n"
if old in text:
    text = text.replace(old, new, 1)
elif new not in text:
    raise SystemExit("RISC-V ABI build anchor not found")
makefile.write_text(text)

support = Path("src/target/riscv64/codegen_support.c")
text = support.read_text()
old_include = '#include "target/riscv64/codegen_internal.h"\n'
new_include = '#include "target/riscv64/codegen_internal.h"\n#include "target/riscv64/abi.h"\n'
if old_include in text:
    text = text.replace(old_include, new_include, 1)
elif new_include not in text:
    raise SystemExit("RISC-V ABI include anchor not found")

old = '''bool minic_riscv64_integer_aggregate_abi(const MinicC0Program *program,
                                         MinicType type,
                                         size_t *storage_size,
                                         size_t *register_chunks) {
    size_t alignment;
    size_t size;

    if (program == NULL || storage_size == NULL || register_chunks == NULL ||
        !minic_type_is_record(type) ||
        !minic_riscv64_integer_aggregate_member_type(program, type) ||
        !minic_riscv64_type_layout(program, type, &size, &alignment) || size == 0U || size > 16U) {
        return false;
    }
    (void)alignment;
    *storage_size = size;
    *register_chunks = (size + 7U) / 8U;
    return true;
}
'''
old_zero = old.replace(" || size == 0U || size > 16U", " || size > 16U")
new = '''bool minic_riscv64_integer_aggregate_abi(const MinicC0Program *program,
                                         MinicType type,
                                         size_t *storage_size,
                                         size_t *register_chunks) {
    MinicRiscv64AbiValue abi_value;

    if (storage_size == NULL || register_chunks == NULL ||
        !minic_riscv64_classify_abi_value(program, type, &abi_value) ||
        (abi_value.kind != MINIC_RISCV64_ABI_VALUE_IGNORE &&
         abi_value.kind != MINIC_RISCV64_ABI_VALUE_AGGREGATE)) {
        return false;
    }
    *storage_size = abi_value.storage_size;
    *register_chunks = abi_value.register_chunks;
    return true;
}
'''
if old in text:
    text = text.replace(old, new, 1)
elif old_zero in text:
    text = text.replace(old_zero, new, 1)
elif new not in text:
    raise SystemExit("aggregate ABI classifier anchor not found")
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

runpy.run_path("tools/dev/materialize-indirect-aggregate-abi.py", run_name="__main__")
