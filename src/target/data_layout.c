#include "target/data_layout.h"

#include <stdint.h>

#define MINIC_DATA_LAYOUT_MAX_DEPTH 64U

static const MinicDataLayout minic_rv64_data_layout = {
    .pointer_size = 8U,
    .pointer_alignment = 8U,
    .integer_size = {0U, 1U, 1U, 2U, 4U, 8U, 8U, 16U},
    .integer_alignment = {0U, 1U, 1U, 2U, 4U, 8U, 8U, 16U},
    .float_size = 4U,
    .float_alignment = 4U,
    .double_size = 8U,
    .double_alignment = 8U,
};

const MinicDataLayout *minic_default_data_layout(void) {
    return &minic_rv64_data_layout;
}

static bool minic_data_layout_align_up(size_t value, size_t alignment, size_t *result) {
    size_t remainder;
    size_t padding;

    if (result == NULL || alignment == 0U) {
        return false;
    }
    remainder = value % alignment;
    padding = remainder == 0U ? 0U : alignment - remainder;
    if (value > SIZE_MAX - padding) {
        return false;
    }
    *result = value + padding;
    return true;
}

static bool minic_data_layout_type_depth(const MinicDataLayout *layout,
                                         const MinicC0Program *program,
                                         MinicType type,
                                         unsigned int depth,
                                         size_t *size,
                                         size_t *alignment);

static bool minic_data_layout_record_depth(const MinicDataLayout *layout,
                                           const MinicC0Program *program,
                                           const MinicRecord *record,
                                           unsigned int depth,
                                           size_t requested_field,
                                           size_t *requested_offset,
                                           size_t *size,
                                           size_t *alignment) {
    size_t storage_size;
    size_t record_alignment;
    size_t index;

    if (layout == NULL || program == NULL || record == NULL || size == NULL || alignment == NULL ||
        !record->is_complete || depth > MINIC_DATA_LAYOUT_MAX_DEPTH) {
        return false;
    }
    storage_size = 0U;
    record_alignment = 1U;
    for (index = 0U; index < record->field_count; ++index) {
        const MinicRecordField *field;
        size_t element_size;
        size_t field_size;
        size_t field_alignment;
        size_t field_offset;

        field = &record->fields[index];
        if (field->element_count == 0U ||
            !minic_data_layout_type_depth(
                layout, program, field->type, depth + 1U, &element_size, &field_alignment) ||
            element_size > SIZE_MAX / field->element_count) {
            return false;
        }
        if (field->is_bit_field) {
            size_t storage_bits;

            if (!minic_type_is_integer(field->type) || field->element_count != 1U ||
                field->is_flexible_array || element_size > SIZE_MAX / 8U) {
                return false;
            }
            storage_bits = element_size * 8U;
            if (field->bit_width > storage_bits ||
                (field->bit_width != 0U && field->bit_width != storage_bits)) {
                return false;
            }
            field_size = field->bit_width == 0U ? 0U : element_size;
            if (record->is_union) {
                field_offset = 0U;
                if (field_size > storage_size) {
                    storage_size = field_size;
                }
            } else if (record->is_packed) {
                field_offset = storage_size;
                if (field_size != 0U) {
                    if (field_offset > SIZE_MAX - field_size) {
                        return false;
                    }
                    storage_size = field_offset + field_size;
                }
            } else {
                if (!minic_data_layout_align_up(storage_size, field_alignment, &field_offset)) {
                    return false;
                }
                if (field_size != 0U) {
                    if (field_offset > SIZE_MAX - field_size) {
                        return false;
                    }
                    storage_size = field_offset + field_size;
                } else {
                    storage_size = field_offset;
                }
            }
            if (!record->is_packed && field_alignment > record_alignment) {
                record_alignment = field_alignment;
            }
        } else {
            field_size = (field->is_flexible_array || field->is_zero_length_array)
                             ? 0U
                             : element_size * field->element_count;
            if (field->explicit_alignment != 0U) {
                if ((field->explicit_alignment & (field->explicit_alignment - 1U)) != 0U) {
                    return false;
                }
                if (field->explicit_alignment > field_alignment) {
                    field_alignment = field->explicit_alignment;
                }
            }
            if (record->is_union) {
                field_offset = 0U;
                if (field_size > storage_size) {
                    storage_size = field_size;
                }
            } else if (record->is_packed && field->explicit_alignment == 0U) {
                field_offset = storage_size;
                if (field_offset > SIZE_MAX - field_size) {
                    return false;
                }
                storage_size = field_offset + field_size;
            } else {
                if (!minic_data_layout_align_up(storage_size, field_alignment, &field_offset) ||
                    field_offset > SIZE_MAX - field_size) {
                    return false;
                }
                storage_size = field_offset + field_size;
            }
            if ((!record->is_packed || field->explicit_alignment != 0U) &&
                field_alignment > record_alignment) {
                record_alignment = field_alignment;
            }
        }
        if (requested_offset != NULL && index == requested_field) {
            *requested_offset = field_offset;
        }
    }
    if (record->explicit_alignment != 0U) {
        if ((record->explicit_alignment & (record->explicit_alignment - 1U)) != 0U) {
            return false;
        }
        if (record->explicit_alignment > record_alignment) {
            record_alignment = record->explicit_alignment;
        }
    }
    if (!minic_data_layout_align_up(storage_size, record_alignment, size)) {
        return false;
    }
    *alignment = record_alignment;
    return true;
}

static bool minic_data_layout_type_depth(const MinicDataLayout *layout,
                                         const MinicC0Program *program,
                                         MinicType type,
                                         unsigned int depth,
                                         size_t *size,
                                         size_t *alignment) {
    if (layout == NULL || program == NULL || size == NULL || alignment == NULL ||
        depth > MINIC_DATA_LAYOUT_MAX_DEPTH) {
        return false;
    }
    if (minic_type_is_pointer(type)) {
        *size = layout->pointer_size;
        *alignment = layout->pointer_alignment;
        return true;
    }
    if (minic_type_is_integer(type)) {
        size_t rank = (size_t)type.integer_rank;

        if (rank == (size_t)MINIC_INTEGER_RANK_NONE || rank > (size_t)MINIC_INTEGER_RANK_INT128 ||
            layout->integer_size[rank] == 0U || layout->integer_alignment[rank] == 0U) {
            return false;
        }
        *size = layout->integer_size[rank];
        *alignment = layout->integer_alignment[rank];
        return true;
    }
    if (minic_type_is_float(type)) {
        *size = layout->float_size;
        *alignment = layout->float_alignment;
        return true;
    }
    if (minic_type_is_double(type)) {
        *size = layout->double_size;
        *alignment = layout->double_alignment;
        return true;
    }
    if (minic_type_is_array(type)) {
        const MinicArrayType *array_type;
        size_t element_size;
        size_t element_alignment;

        array_type = minic_c0_program_array_type(program, type.array_type_id);
        if (array_type == NULL || array_type->element_count == 0U ||
            !minic_data_layout_type_depth(layout,
                                          program,
                                          array_type->element_type,
                                          depth + 1U,
                                          &element_size,
                                          &element_alignment) ||
            element_size > SIZE_MAX / array_type->element_count) {
            return false;
        }
        *size = element_size * array_type->element_count;
        *alignment = element_alignment;
        return true;
    }
    if (minic_type_is_record(type)) {
        const MinicRecord *record;

        record = minic_c0_program_record(program, type.record_id);
        return minic_data_layout_record_depth(
            layout, program, record, depth + 1U, SIZE_MAX, NULL, size, alignment);
    }
    return false;
}

bool minic_data_layout_type(const MinicDataLayout *layout,
                            const MinicC0Program *program,
                            MinicType type,
                            size_t *size,
                            size_t *alignment) {
    return minic_data_layout_type_depth(layout, program, type, 0U, size, alignment);
}

bool minic_data_layout_record_field_offset(const MinicDataLayout *layout,
                                           const MinicC0Program *program,
                                           const MinicRecord *record,
                                           size_t field_index,
                                           size_t *offset) {
    size_t size;
    size_t alignment;

    if (record == NULL || offset == NULL || field_index >= record->field_count) {
        return false;
    }
    return minic_data_layout_record_depth(
        layout, program, record, 0U, field_index, offset, &size, &alignment);
}
