#include "target/common/lp64_aggregate.h"

/* The two target ABIs currently share these specifically tested predicates,
 * but register numbering, stack assignment and argument emission do not. */
/* Integer-class LP64 record arguments, shared only as ABI classification.
 * Layout remains target-owned; register placement is ISA-specific.
 * Never claim HFAs, packed/unaligned records, bitfields or vector ABI.
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
    if (record == NULL || !record->is_complete || record->is_packed ||
        record->pack_alignment != 0U || record->field_count == 0U)
        return false;
    for (i = 0U; i < record->field_count; ++i) {
        const MinicRecordField *field = minic_c0_record_field(record, i);
        size_t offset, field_size, field_align;
        if (field == NULL || field->is_packed || field->is_bit_field ||
            field->is_flexible_array || field->is_array ||
            field->element_count != 1U ||
            !minic_multi_integer_record_members(
                 program, target, field->type, depth + 1U) ||
            !minic_data_layout_type(target->data_layout, program, field->type,
                                    &field_size, &field_align) ||
            !minic_data_layout_record_field_offset(
                 target->data_layout, program, record, i, &offset) ||
            (field_align != 0U && offset % field_align != 0U) ||
            offset > total || field_size > total - offset)
            return false;
    }
    return true;
}

bool minic_multi_gp_record(const MinicC0Program *program,
                                   const MinicTargetInfo *target,
                                   MinicType type, size_t *bytes,
                                   size_t *eightbytes) {
    size_t size, align;
    if (bytes == NULL || eightbytes == NULL || !minic_type_is_record(type) ||
        !minic_multi_integer_record_members(program, target, type, 0U) ||
        !minic_data_layout_type(target->data_layout, program, type,
                                &size, &align) ||
        size == 0U || size > 16U || align == 0U || align > 8U)
        return false;
    *bytes = size;
    *eightbytes = (size + 7U) / 8U;
    return true;
}

/* Both LP64 ABIs pass >32-byte aggregate arguments by a pointer to a
 * caller-owned copy. This range excludes all 1-4 element FP/HFA classes;
 * no guessed classification for exotic or over-aligned structures. */
bool minic_multi_indirect_record(const MinicC0Program *program,
                                        const MinicTargetInfo *target,
                                        MinicType type, size_t *bytes) {
    size_t size, align;
    if (program == NULL || target == NULL || bytes == NULL ||
        !minic_type_is_record(type) ||
        !minic_data_layout_type(target->data_layout, program, type,
                                &size, &align) ||
        size <= 32U || size > 2048U || align == 0U || align > 16U)
        return false;
    *bytes = size;
    return true;
}

bool minic_multi_fp_return_record(
    const MinicC0Program *program, const MinicTargetInfo *target,
    MinicType type, size_t *element_count, size_t *element_width) {
    const MinicRecord *record;
    const MinicRecordField *first;
    size_t total, align, width, i;
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
    if (!minic_data_layout_type(target->data_layout, program, type,
                                &total, &align) ||
        total != width * record->field_count || align != width)
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
