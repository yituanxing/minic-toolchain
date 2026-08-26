#!/usr/bin/env python3
from pathlib import Path

PATH = Path("src/target/riscv64/core_codegen.c")
text = PATH.read_text()


def replace_once(old: str, new: str, label: str) -> None:
    global text
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"M164 tail trace {label}: expected 1 match, got {count}")
    text = text.replace(old, new, 1)

replace_once(
    '''        if (!minic_riscv64_abi_cursor_initialize_for_return(
                program, function->return_type, &cursor, &return_value) ||
            (return_value.kind != MINIC_RISCV64_ABI_VALUE_VOID &&
             return_value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER &&
             (return_value.kind != MINIC_RISCV64_ABI_VALUE_AGGREGATE ||
              return_value.slot_count == 0U || return_value.slot_count > 2U))) {
            return false;
        }
''',
    '''        if (!minic_riscv64_abi_cursor_initialize_for_return(
                program, function->return_type, &cursor, &return_value) ||
            (return_value.kind != MINIC_RISCV64_ABI_VALUE_VOID &&
             return_value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER &&
             (return_value.kind != MINIC_RISCV64_ABI_VALUE_AGGREGATE ||
              return_value.slot_count == 0U || return_value.slot_count > 2U))) {
            fprintf(stderr, "M164_PREFLIGHT reason=return-abi kind=%d size=%zu slots=%zu\\n",
                    (int)return_value.kind, return_value.storage_size, return_value.slot_count);
            return false;
        }
''',
    "return-abi",
)

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
                    program, function->parameter_types[index], true, &cursor, &location) ||
                (location.value.kind != MINIC_RISCV64_ABI_VALUE_IGNORE &&
                 location.value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER &&
                 (location.value.kind != MINIC_RISCV64_ABI_VALUE_AGGREGATE ||
                  location.value.slot_count == 0U || location.value.slot_count > 2U))) {
                fprintf(stderr,
                        "M164_PREFLIGHT reason=parameter index=%zu kind=%d size=%zu slots=%zu regs=%zu stack=%zu\\n",
                        index, (int)location.value.kind, location.value.storage_size,
                        location.value.slot_count, location.integer_register_count,
                        location.stack_slot_count);
                return false;
            }
''',
    "parameter",
)

replace_once(
    '''        if ((!core_scalar_type(object_type) && !minic_type_is_record(object_type)) ||
            !minic_data_layout_type(minic_default_data_layout(),
                                    program,
                                    object_type,
                                    &object_size,
                                    &object_alignment) ||
            (object_size == 0U && !minic_type_is_record(object_type)) ||
            object_alignment == 0U || object_alignment > 16U) {
            return false;
        }
''',
    '''        if ((!core_scalar_type(object_type) && !minic_type_is_record(object_type)) ||
            !minic_data_layout_type(minic_default_data_layout(),
                                    program,
                                    object_type,
                                    &object_size,
                                    &object_alignment) ||
            (object_size == 0U && !minic_type_is_record(object_type)) ||
            object_alignment == 0U || object_alignment > 16U) {
            fprintf(stderr,
                    "M164_PREFLIGHT reason=object index=%zu int=%d ptr=%d rec=%d arr=%d size=%zu align=%zu\\n",
                    index, minic_type_is_integer(object_type) ? 1 : 0,
                    minic_type_is_pointer(object_type) ? 1 : 0,
                    minic_type_is_record(object_type) ? 1 : 0,
                    minic_type_is_array(object_type) ? 1 : 0,
                    object_size, object_alignment);
            return false;
        }
''',
    "object",
)

replace_once(
    '''        if (function->globals[index].name == NULL || function->globals[index].name_length == 0U ||
            !core_global_addressable_type(function->globals[index].type)) {
            return false;
        }
''',
    '''        if (function->globals[index].name == NULL || function->globals[index].name_length == 0U ||
            !core_global_addressable_type(function->globals[index].type)) {
            fprintf(stderr,
                    "M164_PREFLIGHT reason=global index=%zu int=%d ptr=%d rec=%d arr=%d void=%d\\n",
                    index,
                    minic_type_is_integer(function->globals[index].type) ? 1 : 0,
                    minic_type_is_pointer(function->globals[index].type) ? 1 : 0,
                    minic_type_is_record(function->globals[index].type) ? 1 : 0,
                    minic_type_is_array(function->globals[index].type) ? 1 : 0,
                    minic_type_is_void(function->globals[index].type) ? 1 : 0);
            return false;
        }
''',
    "global",
)

replace_once(
    '''        if (!core_scalar_type(function->values[index].type)) {
            return false;
        }
''',
    '''        if (!core_scalar_type(function->values[index].type)) {
            fprintf(stderr,
                    "M164_PREFLIGHT reason=value index=%zu int=%d ptr=%d rec=%d arr=%d void=%d\\n",
                    index,
                    minic_type_is_integer(function->values[index].type) ? 1 : 0,
                    minic_type_is_pointer(function->values[index].type) ? 1 : 0,
                    minic_type_is_record(function->values[index].type) ? 1 : 0,
                    minic_type_is_array(function->values[index].type) ? 1 : 0,
                    minic_type_is_void(function->values[index].type) ? 1 : 0);
            return false;
        }
''',
    "value",
)

replace_once(
    '''        if (!core_instruction_supported(program, function, &function->instructions[index])) {
            return false;
        }
''',
    '''        if (!core_instruction_supported(program, function, &function->instructions[index])) {
            fprintf(stderr, "M164_PREFLIGHT reason=instruction index=%zu kind=%d\\n",
                    index, (int)function->instructions[index].kind);
            return false;
        }
''',
    "instruction",
)

PATH.write_text(text)
print("M164_TAIL_TRACE_APPLIED")
