#include "target/common/aggregate_shape.h"
#include "target/common/lp64_aggregate.h"
#include <stdint.h>

/* The two target ABIs currently share these specifically tested predicates,
 * but register numbering, stack assignment and argument emission do not. */
/* Integer-class LP64 record arguments, shared only as ABI classification.
 * Layout remains target-owned; register placement is ISA-specific.
 * Integer-only packed bit-field aggregates still have INTEGER ABI class:
 * copy their complete DataLayout bytes through GP eightbytes, never the
 * individual bit-fields. Reject FP/HFA, vector and unaligned scalar loads.
 * For <=16-byte integral-only aggregates, payload bytes travel as one
 * or two complete integer eightbytes, with the last slot padded to 8. */
static bool minic_multi_integer_record_members(
    const MinicC0Program *program, const MinicTargetInfo *target,
    MinicType type, unsigned int depth) {
    const MinicRecord *record;
    size_t total, alignment, i;
    if (program == NULL || target == NULL || depth > 16U ||
        !minic_data_layout_type(target->data_layout, program, type,
                                &total, &alignment) ||
        total == 0U || alignment == 0U || alignment > 8U)
        return false;
    if (minic_type_is_integer(type) || minic_type_is_pointer(type))
        return total <= 8U;
    if (!minic_type_is_record(type)) return false;
    record = minic_c0_program_record(program, type.record_id);
    if (record == NULL || !record->is_complete ||
        record->field_count == 0U)
        return false;
    for (i = 0U; i < record->field_count; ++i) {
        const MinicRecordField *field = minic_c0_record_field(record, i);
        size_t offset, field_size, field_align;
        /* A fixed C array of integer/pointer elements remains an INTEGER
         * class aggregate on both AAPCS64 and LoongArch LP64D. A literal
         * struct { unsigned char bytes[12]; } has twelve value bytes, not
         * twelve ABI registers; classify by the enclosing 1-2 eightbytes.
         * Reuse DataLayout's element size and natural member alignment.
         * Reject flexible/zero length arrays, floats/HFAs, and storage
         * overflow. Packed integer bit-fields are INTEGER class when
         * DataLayout proves that their bytes belong to this record.
         * Ordinary fields must remain naturally aligned: native direct
         * LP64 loads are not yet an unaligned-word access ABI. */
        if (field == NULL ||
            field->is_flexible_array || field->is_zero_length_array ||
            field->element_count == 0U ||
            (!field->is_array && field->element_count != 1U) ||
            (field->is_bit_field &&
             (field->is_array || !minic_type_is_integer(field->type) ||
              field->element_count != 1U || field->bit_width == 0U)) ||
            !minic_multi_integer_record_members(
                 program, target, field->type, depth + 1U) ||
            !minic_data_layout_type(target->data_layout, program, field->type,
                                    &field_size, &field_align) ||
            field_size > SIZE_MAX / field->element_count)
            return false;
        field_size *= field->element_count;
        /* A named natural C integer bit-field is also carried in the
         * enclosing INTEGER eightbyte(s). Let DataLayout own bit/byte
         * placement: a field starting inside a storage unit need not have
         * a byte offset aligned to its declared base type. The assembly
         * producer moves the full raw object bytes, preserving GNU packing
         * and the caller's signed-bitfield interpretation. */
        if ((field->is_bit_field &&
             field->bit_width > field_size * 8U) ||
            !minic_data_layout_record_field_offset(
                 target->data_layout, program, record, i, &offset) ||
            (!field->is_bit_field && field_align != 0U &&
             offset % field_align != 0U) ||
            offset >= total ||
            (!field->is_bit_field && field_size > total - offset))
            return false;
    }
    return true;
}

bool minic_multi_gp_record(const MinicC0Program *program,
                                   const MinicTargetInfo *target,
                                   MinicType type, size_t *bytes,
                                   size_t *eightbytes) {
    MinicAggregateStorageShape shape;
    if (bytes == NULL || eightbytes == NULL || !minic_type_is_record(type) ||
        !minic_multi_integer_record_members(program, target, type, 0U) ||
        !minic_common_aggregate_storage_shape(
            program, target->data_layout, type, 8U, &shape) ||
        shape.bytes == 0U || shape.bytes > 16U ||
        shape.alignment > 8U)
        return false;
    *bytes = shape.bytes;
    *eightbytes = shape.words;
    return true;
}

/* Both LP64 ABIs pass >32-byte aggregate arguments by a pointer to a
 * caller-owned copy. This range excludes all 1-4 element FP/HFA classes;
 * no guessed classification for exotic or over-aligned structures. */
bool minic_multi_indirect_record(const MinicC0Program *program,
                                        const MinicTargetInfo *target,
                                        MinicType type, size_t *bytes) {
    MinicAggregateStorageShape shape;
    if (program == NULL || target == NULL || bytes == NULL ||
        !minic_type_is_record(type) ||
        !minic_common_aggregate_storage_shape(
            program, target->data_layout, type, 8U, &shape) ||
        shape.bytes <= 32U || shape.bytes > 2048U ||
        shape.alignment > 16U)
        return false;
    *bytes = shape.bytes;
    return true;
}

bool minic_multi_fp_return_record(
    const MinicC0Program *program, const MinicTargetInfo *target,
    MinicType type, size_t *element_count, size_t *element_width) {
    const MinicRecord *record;
    const MinicRecordField *first;
    MinicAggregateStorageShape shape;
    size_t width, i;
    if (program == NULL || target == NULL || element_count == NULL ||
        element_width == NULL || !minic_type_is_record(type))
        return false;
    record = minic_c0_program_record(program, type.record_id);
    if (record == NULL || !record->is_complete || record->is_union ||
        record->is_packed || record->pack_alignment != 0U ||
        record->explicit_alignment != 0U ||
        record->field_count == 0U || record->field_count > 2U)
        return false;
    first = minic_c0_record_field(record, 0U);
    if (first == NULL || first->is_array || first->is_bit_field ||
        first->is_packed || first->is_flexible_array ||
        first->element_count != 1U ||
        (!minic_type_is_float(first->type) &&
         !minic_type_is_double(first->type)))
        return false;
    width = minic_type_is_float(first->type) ? 4U : 8U;
    if (!minic_common_aggregate_storage_shape(
            program, target->data_layout, type, 8U, &shape) ||
        shape.bytes != width * record->field_count ||
        shape.alignment != width)
        return false;
    for (i = 0U; i < record->field_count; ++i) {
        const MinicRecordField *field = minic_c0_record_field(record, i);
        size_t offset;
        if (field == NULL || field->is_array || field->is_packed ||
            field->is_bit_field || field->is_flexible_array ||
            field->element_count != 1U ||
            !minic_type_equal(field->type, first->type) ||
            !minic_data_layout_record_field_offset(
                target->data_layout, program, record, i, &offset) ||
            offset != i * width)
            return false;
    }
    *element_count = record->field_count;
    *element_width = width;
    return true;
}
