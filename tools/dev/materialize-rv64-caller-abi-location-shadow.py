#!/usr/bin/env python3
from pathlib import Path

path = Path("src/target/riscv64/codegen_expression.c")
source = path.read_text()

include_anchor = '#include "target/riscv64/codegen_internal.h"\n'
abi_include = '#include "target/riscv64/abi.h"\n'
if abi_include not in source:
    if include_anchor not in source:
        raise SystemExit("caller ABI shadow include anchor missing")
    source = source.replace(include_anchor, include_anchor + abi_include, 1)

count_anchor = """        {\n            size_t integer_register_index;\n            size_t floating_register_index;\n\n            integer_register_index = 0U;\n            floating_register_index = 0U;\n            for (argument_index = 0U; argument_index < argument_count; ++argument_index) {\n                bool fixed_floating;\n"""
insert_at = source.find(count_anchor)
if insert_at < 0:
    raise SystemExit("caller ABI shadow count anchor missing")
if source.find(count_anchor, insert_at + 1) >= 0:
    raise SystemExit("caller ABI shadow count anchor is not unique")

shadow = r'''        {
            MinicRiscv64AbiCursor shadow_cursor;
            size_t expected_integer_register_count;
            size_t expected_floating_register_count;
            size_t expected_stack_slot_count;

            minic_riscv64_abi_cursor_initialize(&shadow_cursor);
            expected_integer_register_count = 0U;
            expected_floating_register_count = 0U;
            expected_stack_slot_count = 0U;
            for (argument_index = 0U; argument_index < argument_count; ++argument_index) {
                const MinicExpression *argument;
                const MinicRiscv64AbiValue *value;
                MinicRiscv64AbiArgumentLocation location;
                MinicType placement_type;
                size_t expected_integer_begin;
                size_t expected_integer_count;
                size_t expected_floating_begin;
                size_t expected_floating_count;
                size_t expected_stack_begin;
                size_t expected_stack_count;
                size_t slots;
                size_t available_integer_registers;
                bool is_fixed_parameter;

                argument = minic_c0_program_expression(
                    program, expression->value.call.arguments[argument_index]);
                if (argument == NULL) {
                    fprintf(stderr, "CALLER_ABI_SHADOW_FAIL reason=argument arg=%zu\n", argument_index);
                    return false;
                }
                value = &abi_values[argument_index];
                is_fixed_parameter = argument_index < parameter_count;
                placement_type = is_fixed_parameter ? abi_parameter_types[argument_index]
                                                    : argument->type;
                if (!minic_riscv64_abi_place_argument(
                        program, placement_type, is_fixed_parameter, &shadow_cursor, &location)) {
                    fprintf(stderr,
                            "CALLER_ABI_SHADOW_FAIL reason=place arg=%zu kind=%d size=%zu chunks=%zu\n",
                            argument_index,
                            (int)value->kind,
                            value->storage_size,
                            value->register_chunks);
                    return false;
                }

                expected_integer_begin = expected_integer_register_count;
                expected_integer_count = 0U;
                expected_floating_begin = expected_floating_register_count;
                expected_floating_count = 0U;
                expected_stack_begin = expected_stack_slot_count;
                expected_stack_count = 0U;

                if (value->kind == MINIC_RISCV64_ABI_VALUE_IGNORE) {
                    slots = 0U;
                } else if (is_fixed_parameter && value->kind == MINIC_RISCV64_ABI_VALUE_FLOAT) {
                    if (expected_floating_register_count >= 8U) {
                        fprintf(stderr,
                                "CALLER_ABI_SHADOW_FAIL reason=old-fp-overflow arg=%zu\n",
                                argument_index);
                        return false;
                    }
                    expected_floating_begin = expected_floating_register_count;
                    expected_floating_count = 1U;
                    expected_floating_register_count += 1U;
                    slots = 0U;
                } else {
                    slots = value->kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE
                                ? value->register_chunks
                                : 1U;
                    available_integer_registers = 8U - expected_integer_register_count;
                    expected_integer_begin = expected_integer_register_count;
                    expected_integer_count = slots < available_integer_registers
                                                 ? slots
                                                 : available_integer_registers;
                    expected_stack_begin = expected_stack_slot_count;
                    expected_stack_count = slots - expected_integer_count;
                    expected_integer_register_count += expected_integer_count;
                    expected_stack_slot_count += expected_stack_count;
                }

                if (location.value.kind != value->kind ||
                    location.value.storage_size != value->storage_size ||
                    location.integer_register_begin != expected_integer_begin ||
                    location.integer_register_count != expected_integer_count ||
                    location.floating_register_begin != expected_floating_begin ||
                    location.floating_register_count != expected_floating_count ||
                    location.stack_slot_begin != expected_stack_begin ||
                    location.stack_slot_count != expected_stack_count) {
                    fprintf(stderr,
                            "CALLER_ABI_SHADOW_MISMATCH arg=%zu kind=%d/%d size=%zu/%zu "
                            "ireg=%zu+%zu/%zu+%zu freg=%zu+%zu/%zu+%zu stack=%zu+%zu/%zu+%zu\n",
                            argument_index,
                            (int)location.value.kind,
                            (int)value->kind,
                            location.value.storage_size,
                            value->storage_size,
                            location.integer_register_begin,
                            location.integer_register_count,
                            expected_integer_begin,
                            expected_integer_count,
                            location.floating_register_begin,
                            location.floating_register_count,
                            expected_floating_begin,
                            expected_floating_count,
                            location.stack_slot_begin,
                            location.stack_slot_count,
                            expected_stack_begin,
                            expected_stack_count);
                    return false;
                }
            }
        }
'''

source = source[:insert_at] + shadow + source[insert_at:]
path.write_text(source)
print("MATERIALIZED rv64-caller-abi-location-shadow")
