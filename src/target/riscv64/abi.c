#include "target/riscv64/abi.h"

#include "target/data_layout.h"

#include <stdint.h>

#define MINIC_RISCV64_ABI_ARGUMENT_REGISTER_COUNT 8U

static bool minic_riscv64_abi_integer_aggregate_member_type(const MinicC0Program *program,
                                                            MinicType type) {
    if (minic_type_is_integer(type) || minic_type_is_pointer(type)) {
        return true;
    }
    if (minic_type_is_array(type)) {
        const MinicArrayType *array_type;

        array_type = minic_c0_program_array_type(program, type.array_type_id);
        return array_type != NULL &&
               minic_riscv64_abi_integer_aggregate_member_type(program, array_type->element_type);
    }
    if (minic_type_is_record(type)) {
        const MinicRecord *record;
        size_t field_index;

        record = minic_c0_program_record(program, type.record_id);
        if (record == NULL || !record->is_complete) {
            return false;
        }
        for (field_index = 0U; field_index < record->field_count; ++field_index) {
            const MinicRecordField *field;

            field = minic_c0_record_field(record, field_index);
            if (field == NULL ||
                !minic_riscv64_abi_integer_aggregate_member_type(program, field->type)) {
                return false;
            }
        }
        return true;
    }
    return false;
}

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
    MinicRiscv64AbiValue result;
    size_t alignment;
    size_t size;

    if (program == NULL || value == NULL) {
        return false;
    }

    result.kind = MINIC_RISCV64_ABI_VALUE_INVALID;
    result.storage_size = 0U;
    result.slot_count = 0U;

    if (minic_type_is_void(type)) {
        result.kind = MINIC_RISCV64_ABI_VALUE_VOID;
        *value = result;
        return true;
    }
    if (!minic_data_layout_type(minic_default_data_layout(), program, type, &size, &alignment)) {
        return false;
    }
    (void)alignment;

    if (minic_type_is_integer(type) || minic_type_is_pointer(type)) {
        result.kind = MINIC_RISCV64_ABI_VALUE_INTEGER;
        result.storage_size = size;
        result.slot_count = 1U;
        *value = result;
        return true;
    }
    if (minic_type_is_float(type) || minic_type_is_double(type)) {
        result.kind = MINIC_RISCV64_ABI_VALUE_FLOAT;
        result.storage_size = size;
        result.slot_count = 1U;
        *value = result;
        return true;
    }
    if (minic_type_is_record(type) &&
        minic_riscv64_abi_integer_aggregate_member_type(program, type) &&
        (size == 8U || size == 16U)) {
        result.kind = MINIC_RISCV64_ABI_VALUE_AGGREGATE;
        result.storage_size = size;
        result.slot_count = size / 8U;
        *value = result;
        return true;
    }
    return false;
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
        cursor->floating_register_count > MINIC_RISCV64_ABI_ARGUMENT_REGISTER_COUNT) {
        return false;
    }
    if (!minic_riscv64_abi_classify_value(program, type, &result.value) ||
        result.value.kind == MINIC_RISCV64_ABI_VALUE_VOID) {
        return false;
    }

    result.integer_register_begin = cursor->integer_register_count;
    result.integer_register_count = 0U;
    result.floating_register_begin = cursor->floating_register_count;
    result.floating_register_count = 0U;
    result.stack_slot_begin = cursor->stack_slot_count;
    result.stack_slot_count = 0U;
    next = *cursor;

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
    if (!is_fixed_parameter && result.value.kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE) {
        return false;
    }

    integer_slots = result.value.kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE ? result.value.slot_count
                                                                           : 1U;
    available_integer_registers =
        MINIC_RISCV64_ABI_ARGUMENT_REGISTER_COUNT - next.integer_register_count;
    result.integer_register_begin = next.integer_register_count;
    result.integer_register_count =
        integer_slots < available_integer_registers ? integer_slots : available_integer_registers;
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
