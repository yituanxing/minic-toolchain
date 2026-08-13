#include "target/riscv64/abi.h"

#include "target/riscv64/layout.h"

static bool minic_riscv64_integer_aggregate_member_type(const MinicC0Program *program,
                                                        MinicType type) {
    if (minic_type_is_integer(type) || minic_type_is_pointer(type)) {
        return true;
    }
    if (minic_type_is_array(type)) {
        const MinicArrayType *array_type;

        array_type = minic_c0_program_array_type(program, type.array_type_id);
        return array_type != NULL &&
               minic_riscv64_integer_aggregate_member_type(program, array_type->element_type);
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
                !minic_riscv64_integer_aggregate_member_type(program, field->type)) {
                return false;
            }
        }
        return true;
    }
    return false;
}

bool minic_riscv64_classify_abi_value(const MinicC0Program *program,
                                      MinicType type,
                                      MinicRiscv64AbiValue *result) {
    size_t alignment;
    size_t size;

    if (program == NULL || result == NULL ||
        !minic_riscv64_type_layout(program, type, &size, &alignment)) {
        return false;
    }
    (void)alignment;

    result->storage_size = size;
    result->register_chunks = 0U;

    if (minic_type_is_record(type)) {
        if (size == 0U) {
            result->kind = MINIC_RISCV64_ABI_VALUE_IGNORE;
            return true;
        }
        if (minic_riscv64_integer_aggregate_member_type(program, type) && size <= 16U) {
            result->kind = MINIC_RISCV64_ABI_VALUE_AGGREGATE;
            result->register_chunks = (size + 7U) / 8U;
            return true;
        }
        result->kind = MINIC_RISCV64_ABI_VALUE_INDIRECT;
        return true;
    }

    if (minic_type_is_float(type) || minic_type_is_double(type)) {
        result->kind = MINIC_RISCV64_ABI_VALUE_FLOAT;
        result->register_chunks = 1U;
        return true;
    }
    if (minic_type_is_integer(type) || minic_type_is_pointer(type)) {
        result->kind = MINIC_RISCV64_ABI_VALUE_INTEGER;
        result->register_chunks = 1U;
        return true;
    }
    return false;
}
