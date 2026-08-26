#!/usr/bin/env python3
from pathlib import Path

PATH = Path("src/target/riscv64/core_codegen.c")
text = PATH.read_text()


def replace_once(old: str, new: str, label: str) -> None:
    global text
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"M163b zero-record ABI {label}: expected 1 match, got {count}")
    text = text.replace(old, new, 1)


# Zero-size GNU records are addressable Core objects but consume no storage.
replace_once(
    '''            object_size == 0U || object_alignment == 0U || object_alignment > 16U ||
            function->objects[object_index].element_count == 0U ||
''',
    '''            (object_size == 0U &&
             !minic_type_is_record(function->objects[object_index].type)) ||
            object_alignment == 0U || object_alignment > 16U ||
            function->objects[object_index].element_count == 0U ||
''',
    "frame-zero-record",
)

replace_once(
    '''            object_size == 0U || object_alignment == 0U || object_alignment > 16U ||
            function->objects[object_index].element_count == 0U ||
            object_size > SIZE_MAX / function->objects[object_index].element_count ||
            !align_up(current_offset, object_alignment, &current_offset)) {
''',
    '''            (object_size == 0U &&
             !minic_type_is_record(function->objects[object_index].type)) ||
            object_alignment == 0U || object_alignment > 16U ||
            function->objects[object_index].element_count == 0U ||
            object_size > SIZE_MAX / function->objects[object_index].element_count ||
            !align_up(current_offset, object_alignment, &current_offset)) {
''',
    "offset-zero-record",
)

replace_once(
    '''            object_size == 0U || object_alignment == 0U || object_alignment > 16U) {
            return false;
        }
''',
    '''            (object_size == 0U && !minic_type_is_record(object_type)) ||
            object_alignment == 0U || object_alignment > 16U) {
            return false;
        }
''',
    "preflight-zero-record",
)

# An ABI-ignored empty aggregate has a CoreObject identity but no incoming bytes.
old_parameter_object = '''    if (!core_parameter_location(
            program, function, instruction->value.parameter_object.parameter_index, &location) ||
        location.value.kind != MINIC_RISCV64_ABI_VALUE_AGGREGATE ||
        location.value.slot_count == 0U || location.value.slot_count > 2U ||
        location.value.slot_count != location.integer_register_count + location.stack_slot_count ||
        object_id >= function->object_count ||
        !minic_type_unqualified(function->objects[object_id].type, &object_value_type) ||
        !minic_type_equal(
            object_value_type,
            function->parameter_types[instruction->value.parameter_object.parameter_index]) ||
        !core_object_offset(program, function, object_id, &object_offset)) {
        return false;
    }
'''
new_parameter_object = '''    if (!core_parameter_location(
            program, function, instruction->value.parameter_object.parameter_index, &location) ||
        object_id >= function->object_count ||
        !minic_type_unqualified(function->objects[object_id].type, &object_value_type) ||
        !minic_type_equal(
            object_value_type,
            function->parameter_types[instruction->value.parameter_object.parameter_index]) ||
        !core_object_offset(program, function, object_id, &object_offset)) {
        return false;
    }
    if (location.value.kind == MINIC_RISCV64_ABI_VALUE_IGNORE) {
        return location.value.slot_count == 0U && location.integer_register_count == 0U &&
               location.stack_slot_count == 0U;
    }
    if (location.value.kind != MINIC_RISCV64_ABI_VALUE_AGGREGATE ||
        location.value.slot_count == 0U || location.value.slot_count > 2U ||
        location.value.slot_count != location.integer_register_count + location.stack_slot_count) {
        return false;
    }
'''
replace_once(old_parameter_object, new_parameter_object, "ignored-parameter-object")

# Direct calls may likewise contain an ignored empty-record OBJECT argument.
old_call_record = '''            if (location.value.kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE) {
                if (location.value.slot_count == 0U || location.value.slot_count > 2U ||
                    location.stack_slot_count != 0U ||
                    location.integer_register_count != location.value.slot_count ||
                    location.integer_register_begin + location.integer_register_count > 8U) {
                    return false;
                }
            } else if (location.value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
'''
new_call_record = '''            if (location.value.kind == MINIC_RISCV64_ABI_VALUE_IGNORE) {
                if (location.value.slot_count != 0U || location.integer_register_count != 0U ||
                    location.stack_slot_count != 0U) {
                    return false;
                }
            } else if (location.value.kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE) {
                if (location.value.slot_count == 0U || location.value.slot_count > 2U ||
                    location.stack_slot_count != 0U ||
                    location.integer_register_count != location.value.slot_count ||
                    location.integer_register_begin + location.integer_register_count > 8U) {
                    return false;
                }
            } else if (location.value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
'''
replace_once(old_call_record, new_call_record, "ignored-call-preflight")

old_emit_object = '''            if (location.value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
                if (location.integer_register_count != 1U || location.stack_slot_count != 0U ||
                    location.integer_register_begin >= 8U ||
                    !emit_sp_address(file,
                                     minic_core_rv64_argument_registers[
                                         location.integer_register_begin],
                                     object_offset)) {
                    return false;
                }
                continue;
            }
            if (location.value.kind != MINIC_RISCV64_ABI_VALUE_AGGREGATE) {
                return false;
            }
'''
new_emit_object = '''            if (location.value.kind == MINIC_RISCV64_ABI_VALUE_IGNORE) {
                if (location.value.slot_count != 0U || location.integer_register_count != 0U ||
                    location.stack_slot_count != 0U) {
                    return false;
                }
                continue;
            }
            if (location.value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
                if (location.integer_register_count != 1U || location.stack_slot_count != 0U ||
                    location.integer_register_begin >= 8U ||
                    !emit_sp_address(file,
                                     minic_core_rv64_argument_registers[
                                         location.integer_register_begin],
                                     object_offset)) {
                    return false;
                }
                continue;
            }
            if (location.value.kind != MINIC_RISCV64_ABI_VALUE_AGGREGATE) {
                return false;
            }
'''
# The same shape still exists in indirect-call emission; patch only direct emit_call region.
start = text.index("static bool emit_call(FILE *file,")
end = text.index("static bool emit_indirect_call(FILE *file,", start)
region = text[start:end]
if region.count(old_emit_object) != 1:
    raise SystemExit(
        f"M163b zero-record ABI ignored-call-emitter: expected 1 emit_call match, got {region.count(old_emit_object)}"
    )
text = text[:start] + region.replace(old_emit_object, new_emit_object, 1) + text[end:]

PATH.write_text(text)
print("M163B_ZERO_RECORD_ABI_APPLIED")
