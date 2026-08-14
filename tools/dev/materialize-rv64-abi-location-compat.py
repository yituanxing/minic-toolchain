#!/usr/bin/env python3
from pathlib import Path

header_path = Path("src/target/riscv64/abi.h")
source_path = Path("src/target/riscv64/abi.c")
header = header_path.read_text()
source = source_path.read_text()

field_anchor = """typedef struct MinicRiscv64AbiValue {\n    MinicRiscv64AbiValueKind kind;\n    size_t storage_size;\n    size_t register_chunks;\n} MinicRiscv64AbiValue;\n"""
field_replacement = """typedef struct MinicRiscv64AbiValue {\n    MinicRiscv64AbiValueKind kind;\n    size_t storage_size;\n    size_t register_chunks;\n    size_t slot_count;\n} MinicRiscv64AbiValue;\n\ntypedef struct MinicRiscv64AbiCursor {\n    size_t integer_register_count;\n    size_t floating_register_count;\n    size_t stack_slot_count;\n} MinicRiscv64AbiCursor;\n\ntypedef struct MinicRiscv64AbiArgumentLocation {\n    MinicRiscv64AbiValue value;\n    size_t integer_register_begin;\n    size_t integer_register_count;\n    size_t floating_register_begin;\n    size_t floating_register_count;\n    size_t stack_slot_begin;\n    size_t stack_slot_count;\n} MinicRiscv64AbiArgumentLocation;\n"""
if field_anchor not in header:
    raise SystemExit("discovery ABI value anchor missing")
header = header.replace(field_anchor, field_replacement, 1)

declaration_anchor = """bool minic_riscv64_classify_abi_value(const MinicC0Program *program,\n                                      MinicType type,\n                                      MinicRiscv64AbiValue *result);\n"""
declaration_replacement = declaration_anchor + """\nvoid minic_riscv64_abi_cursor_initialize(MinicRiscv64AbiCursor *cursor);\nbool minic_riscv64_abi_classify_value(const MinicC0Program *program,\n                                      MinicType type,\n                                      MinicRiscv64AbiValue *value);\nbool minic_riscv64_abi_place_argument(const MinicC0Program *program,\n                                      MinicType type,\n                                      bool is_fixed_parameter,\n                                      MinicRiscv64AbiCursor *cursor,\n                                      MinicRiscv64AbiArgumentLocation *location);\n"""
if declaration_anchor not in header:
    raise SystemExit("discovery ABI declaration anchor missing")
header = header.replace(declaration_anchor, declaration_replacement, 1)
header_path.write_text(header)

if "#include <stdint.h>\n" not in source:
    include_anchor = '#include "target/riscv64/layout.h"\n'
    if include_anchor not in source:
        raise SystemExit("ABI source include anchor missing")
    source = source.replace(include_anchor, include_anchor + "\n#include <stdint.h>\n", 1)

source += r'''

#define MINIC_RISCV64_ABI_ARGUMENT_REGISTER_COUNT 8U

void minic_riscv64_abi_cursor_initialize(MinicRiscv64AbiCursor *cursor) {
    if (cursor == NULL) {
        return;
    }
    cursor->integer_register_count = 0U;
    cursor->floating_register_count = 0U;
    cursor->stack_slot_count = 0U;
}

bool minic_riscv64_abi_classify_value(const MinicC0Program *program,
                                      MinicType type,
                                      MinicRiscv64AbiValue *value) {
    if (!minic_riscv64_classify_abi_value(program, type, value)) {
        return false;
    }
    if (value->kind == MINIC_RISCV64_ABI_VALUE_IGNORE) {
        value->slot_count = 0U;
    } else if (value->kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE) {
        value->slot_count = value->register_chunks;
    } else {
        value->slot_count = 1U;
    }
    return true;
}

bool minic_riscv64_abi_place_argument(const MinicC0Program *program,
                                      MinicType type,
                                      bool is_fixed_parameter,
                                      MinicRiscv64AbiCursor *cursor,
                                      MinicRiscv64AbiArgumentLocation *location) {
    MinicRiscv64AbiArgumentLocation result;
    MinicRiscv64AbiCursor next;
    size_t integer_slots;
    size_t available_integer_registers;

    if (cursor == NULL || location == NULL ||
        cursor->integer_register_count > MINIC_RISCV64_ABI_ARGUMENT_REGISTER_COUNT ||
        cursor->floating_register_count > MINIC_RISCV64_ABI_ARGUMENT_REGISTER_COUNT ||
        !minic_riscv64_abi_classify_value(program, type, &result.value)) {
        return false;
    }

    result.integer_register_begin = cursor->integer_register_count;
    result.integer_register_count = 0U;
    result.floating_register_begin = cursor->floating_register_count;
    result.floating_register_count = 0U;
    result.stack_slot_begin = cursor->stack_slot_count;
    result.stack_slot_count = 0U;
    next = *cursor;

    if (result.value.kind == MINIC_RISCV64_ABI_VALUE_IGNORE) {
        *location = result;
        return true;
    }
    if (is_fixed_parameter && result.value.kind == MINIC_RISCV64_ABI_VALUE_FLOAT) {
        if (next.floating_register_count >= MINIC_RISCV64_ABI_ARGUMENT_REGISTER_COUNT) {
            return false;
        }
        result.floating_register_begin = next.floating_register_count;
        result.floating_register_count = 1U;
        next.floating_register_count += 1U;
        *cursor = next;
        *location = result;
        return true;
    }

    integer_slots = result.value.kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE
                        ? result.value.slot_count
                        : 1U;
    available_integer_registers =
        MINIC_RISCV64_ABI_ARGUMENT_REGISTER_COUNT - next.integer_register_count;
    result.integer_register_begin = next.integer_register_count;
    result.integer_register_count = integer_slots < available_integer_registers
                                        ? integer_slots
                                        : available_integer_registers;
    result.stack_slot_begin = next.stack_slot_count;
    result.stack_slot_count = integer_slots - result.integer_register_count;
    if (next.stack_slot_count > SIZE_MAX - result.stack_slot_count) {
        return false;
    }
    next.integer_register_count += result.integer_register_count;
    next.stack_slot_count += result.stack_slot_count;
    *cursor = next;
    *location = result;
    return true;
}
'''
source_path.write_text(source)
print("MATERIALIZED rv64-abi-location-compat")
