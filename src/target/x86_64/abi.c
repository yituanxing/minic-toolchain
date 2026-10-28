#include "target/common/aggregate_shape.h"
#include "target/x86_64/abi.h"
#include "target/data_layout.h"
#include <stdint.h>

/* Extracted SysV AMD64 classifier: integer widths, conservative aggregates
 * and register-bank/stack ingress. The existing rules stay byte-for-byte;
 * target-specific machine emission remains in machine_emit.c and machine_call.c. */

bool minic_x86_64_scalar_width(const MinicC0Program *program,
                             const MinicTargetInfo *target,
                             MinicType type, size_t *bytes, bool *is_signed) {
    unsigned int bits;
    MinicType effective;
    if (bytes == NULL || is_signed == NULL) return false;
    if (minic_type_is_pointer(type)) {
        *bytes = target->data_layout->pointer_size;
        *is_signed = false;
        return *bytes == 8U;
    }
    if (!minic_type_is_integer(type) ||
        !minic_c0_type_effective_integer_type(program, type, &effective) ||
        !minic_target_info_integer_width(target, program, type, &bits) ||
        bits == 0U || bits > 64U) return false;
    *bytes = (bits + 7U) / 8U;
    *is_signed = minic_type_is_signed_integer(effective);
    return *bytes == 1U || *bytes == 2U || *bytes == 4U || *bytes == 8U;
}

/* Values are stored as raw machine bits in eight-byte Core slots.  Floating
 * load/store must not pass through the integer sign-extension path. */
bool minic_x86_64_storage_width(const MinicC0Program *program,
                              const MinicTargetInfo *target, MinicType type,
                              size_t *bytes, bool *sign) {
    if (minic_type_is_float(type) || minic_type_is_double(type)) {
        if (bytes == NULL || sign == NULL) return false;
        *bytes = minic_type_is_float(type) ? 4U : 8U;
        *sign = false;
        return true;
    }
    return minic_x86_64_scalar_width(program, target, type, bytes, sign);
}

/* SysV AMD64: a non-packed, naturally aligned record consisting solely of
 * integer/pointer fields fits one INTEGER eightbyte when its storage is at
 * most eight bytes. This includes struct in_addr (4B) and GNU transparent
 * sockaddr pointer unions (8B). No SSE, nested, packed or bitfield cases
 * are claimed here; do not guess their ABI classification. We currently
 * materialize only exact 1/2/4/8-byte loads to avoid overreading objects. */
bool minic_x86_64_one_gp_record(const MinicC0Program *program,
                              const MinicTargetInfo *target, MinicType type,
                              size_t *record_bytes) {
    const MinicRecord *rec;
    MinicAggregateStorageShape shape;
    size_t size, align, i;
    if (program == NULL || target == NULL || record_bytes == NULL ||
        !minic_type_is_record(type)) return false;
    rec = minic_c0_program_record(program, type.record_id);
    if (rec == NULL || !rec->is_complete || rec->field_count == 0U ||
        rec->is_packed || rec->pack_alignment != 0U ||
        !minic_common_aggregate_storage_shape(
            program, target->data_layout, type, 8U, &shape))
        return false;
    size = shape.bytes;
    align = shape.alignment;
    if ((size != 1U && size != 2U && size != 4U && size != 8U) ||
        align > 8U) return false;
    for (i = 0U; i < rec->field_count; ++i) {
        const MinicRecordField *field = minic_c0_record_field(rec, i);
        size_t bytes, field_align, offset;
        if (field == NULL || field->is_packed || field->is_array ||
            field->is_bit_field || field->is_flexible_array ||
            field->is_zero_length_array || field->element_count != 1U ||
            (!minic_type_is_pointer(field->type) &&
             !minic_type_is_integer(field->type)) ||
            !minic_data_layout_type(target->data_layout, program, field->type,
                                    &bytes, &field_align) ||
            !minic_data_layout_record_field_offset(target->data_layout, program,
                                                    rec, i, &offset) ||
            field_align == 0U || offset % field_align != 0U ||
            offset > size || bytes > size - offset) return false;
    }
    *record_bytes = size;
    return true;
}

/* SQLite's Token is a two-eightbyte INTEGER-class SysV aggregate:
 * one pointer at byte 0, one 32-bit integer at byte 8, size 16.
 * Deliberately do not claim general record ABI classification here.
 * Reject packed/misaligned, floating and nested records until proved. */
bool minic_x86_64_two_gp_record(const MinicC0Program *program,
                              const MinicTargetInfo *target, MinicType type) {
    const MinicRecord *rec;
    const MinicRecordField *a, *b;
    MinicAggregateStorageShape shape;
    size_t a_offset, b_offset, a_bytes, b_bytes, ignored;
    if (program == NULL || target == NULL || !minic_type_is_record(type))
        return false;
    rec = minic_c0_program_record(program, type.record_id);
    if (rec == NULL || !rec->is_complete || rec->is_union ||
        rec->is_packed || rec->field_count != 2U)
        return false;
    a = minic_c0_record_field(rec, 0U);
    b = minic_c0_record_field(rec, 1U);
    if (a == NULL || b == NULL || a->is_packed || b->is_packed ||
        a->is_bit_field || b->is_bit_field ||
        a->element_count != 1U || b->element_count != 1U ||
        a->is_array || b->is_array ||
        !minic_type_is_pointer(a->type) || !minic_type_is_integer(b->type))
        return false;
    return minic_common_aggregate_storage_shape(
               program, target->data_layout, type, 8U, &shape) &&
           minic_data_layout_type(target->data_layout, program, a->type, &a_bytes, &ignored) &&
           minic_data_layout_type(target->data_layout, program, b->type, &b_bytes, &ignored) &&
           minic_data_layout_record_field_offset(target->data_layout, program, rec, 0U, &a_offset) &&
           minic_data_layout_record_field_offset(target->data_layout, program, rec, 1U, &b_offset) &&
           shape.bytes == 16U && shape.alignment == 8U &&
           a_offset == 0U && b_offset == 8U &&
           a_bytes == 8U && b_bytes == 4U;
}

