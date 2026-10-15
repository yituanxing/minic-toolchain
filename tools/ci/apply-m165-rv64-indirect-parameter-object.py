#!/usr/bin/env python3
from pathlib import Path

PATH = Path("src/target/riscv64/core_codegen.c")
text = PATH.read_text()


def replace_once(old: str, new: str, label: str) -> None:
    global text
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"M165 indirect parameter {label}: expected 1 match, got {count}")
    text = text.replace(old, new, 1)


replace_once(
    '''            if (!minic_riscv64_abi_place_argument(
                    program, function->parameter_types[index], true, &cursor, &location) ||
                (location.value.kind != MINIC_RISCV64_ABI_VALUE_IGNORE &&
                 location.value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER &&
                 (location.value.kind != MINIC_RISCV64_ABI_VALUE_AGGREGATE ||
                  location.value.slot_count == 0U || location.value.slot_count > 2U))) {
                return false;
            }
''',
    '''            if (!minic_riscv64_abi_place_argument(
                    program, function->parameter_types[index], true, &cursor, &location)) {
                return false;
            }
            if (location.value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
                if (!minic_type_is_record(function->parameter_types[index]) ||
                    location.value.storage_size == 0U || location.value.slot_count != 1U ||
                    location.floating_register_count != 0U ||
                    !((location.integer_register_count == 1U &&
                       location.integer_register_begin < 8U &&
                       location.stack_slot_count == 0U) ||
                      (location.integer_register_count == 0U &&
                       location.stack_slot_count == 1U))) {
                    return false;
                }
            } else if (location.value.kind != MINIC_RISCV64_ABI_VALUE_IGNORE &&
                       location.value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER &&
                       (location.value.kind != MINIC_RISCV64_ABI_VALUE_AGGREGATE ||
                        location.value.slot_count == 0U || location.value.slot_count > 2U)) {
                return false;
            }
''',
    "callee-preflight",
)

replace_once(
    '''    if (location.value.kind == MINIC_RISCV64_ABI_VALUE_IGNORE) {
        return location.value.slot_count == 0U && location.integer_register_count == 0U &&
               location.stack_slot_count == 0U;
    }
    if (location.value.kind != MINIC_RISCV64_ABI_VALUE_AGGREGATE ||
        location.value.slot_count == 0U || location.value.slot_count > 2U ||
        location.value.slot_count != location.integer_register_count + location.stack_slot_count) {
        return false;
    }
''',
    '''    if (location.value.kind == MINIC_RISCV64_ABI_VALUE_IGNORE) {
        return location.value.slot_count == 0U && location.integer_register_count == 0U &&
               location.stack_slot_count == 0U;
    }
    if (location.value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
        size_t copied;

        if (location.value.storage_size == 0U || location.value.slot_count != 1U ||
            location.floating_register_count != 0U) {
            return false;
        }
        if (location.integer_register_count == 1U && location.stack_slot_count == 0U) {
            size_t register_index = location.integer_register_begin;

            if (register_index >= 8U ||
                fprintf(file,
                        "  mv t1, %s\\n",
                        minic_core_rv64_argument_registers[register_index]) < 0) {
                return false;
            }
        } else if (location.integer_register_count == 0U &&
                   location.stack_slot_count == 1U) {
            size_t incoming_offset;

            if (location.stack_slot_begin > (SIZE_MAX - frame->frame_size) / 8U) {
                return false;
            }
            incoming_offset = frame->frame_size + location.stack_slot_begin * 8U;
            if (!minic_riscv64_emit_sp_load64(file, "t1", incoming_offset)) {
                return false;
            }
        } else {
            return false;
        }
        if (!emit_sp_address(file, "t0", object_offset)) {
            return false;
        }
        copied = 0U;
        while (copied < location.value.storage_size) {
            size_t chunk = location.value.storage_size - copied;
            size_t offset;

            if (chunk > 2048U) {
                chunk = 2048U;
            }
            for (offset = 0U; offset < chunk; ++offset) {
                if (fprintf(file,
                            "  lbu t2, %zu(t1)\\n"
                            "  sb t2, %zu(t0)\\n",
                            offset,
                            offset) < 0) {
                    return false;
                }
            }
            copied += chunk;
            if (copied < location.value.storage_size &&
                fprintf(file,
                        "  addi t0, t0, 2047\\n"
                        "  addi t0, t0, 1\\n"
                        "  addi t1, t1, 2047\\n"
                        "  addi t1, t1, 1\\n") < 0) {
                return false;
            }
        }
        return true;
    }
    if (location.value.kind != MINIC_RISCV64_ABI_VALUE_AGGREGATE ||
        location.value.slot_count == 0U || location.value.slot_count > 2U ||
        location.value.slot_count != location.integer_register_count + location.stack_slot_count) {
        return false;
    }
''',
    "parameter-object-emitter",
)

PATH.write_text(text)
print("M165_INDIRECT_PARAMETER_OBJECT_APPLIED")
