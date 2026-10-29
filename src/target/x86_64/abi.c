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

/* SysV INTEGER classification is transitive through nested records, unions
 * and integral bit fields. GCC passes Linux's packed IO_APIC_route_entry
 * (anonymous union of u64 bit-field structs) in RAX. Packing changes the
 * aggregate alignment but does not force MEMORY when every non-bit-field
 * member remains naturally aligned. An actual unaligned scalar member DOES
 * force MEMORY and must never be admitted by this classifier. */
static bool x86_one_gp_integer_fields(const MinicC0Program *program,
    const MinicTargetInfo *target, MinicType type, unsigned int depth) {
    const MinicRecord *rec;
    MinicType plain;
    size_t size, alignment, i;
    if (!minic_type_unqualified(type, &plain)) return false;
    if (minic_type_is_pointer(plain) || minic_type_is_integer(plain))
        return true;
    if (!minic_type_is_record(plain) || depth > 12U) return false;
    rec = minic_c0_program_record(program, plain.record_id);
    if (!rec || !rec->is_complete || rec->field_count == 0U ||
        !minic_data_layout_type(target->data_layout, program, plain,
                                &size, &alignment) ||
        !size || size > 8U || !alignment || alignment > 8U)
        return false;
    for (i = 0U; i < rec->field_count; ++i) {
        const MinicRecordField *field = minic_c0_record_field(rec, i);
        size_t bytes, field_align, offset, bit_offset;
        if (!field || field->is_array || field->is_flexible_array ||
            field->is_zero_length_array || field->element_count != 1U ||
            !minic_data_layout_type(target->data_layout, program, field->type,
                                    &bytes, &field_align) ||
            !minic_data_layout_record_field_layout(target->data_layout, program,
                                                    rec, i, &offset, &bit_offset) ||
            !field_align || offset > size)
            return false;
        if (field->is_bit_field) {
            /* A bit-field is INTEGER-classified by its storage unit even
             * when that unit has packed alignment. Reject a crossing of
             * the sole SysV eightbyte or a non-integral base type. */
            if (!minic_type_is_integer(field->type) ||
                bit_offset >= 8U || field->bit_width > 64U ||
                offset > 8U || bit_offset + field->bit_width >
                    (8U - offset) * 8U ||
                bit_offset + field->bit_width > (size - offset) * 8U)
                return false;
        } else if (!x86_one_gp_integer_fields(program, target, field->type,
                                              depth + 1U) ||
                   /* A real unaligned non-bit-field member is MEMORY. */
                   offset % field_align || bytes > size - offset)
            return false;
    }
    return true;
}

/* SysV AMD64: aligned-member <=8-byte INTEGER aggregate.
 * Materialization still restricts loads to exact 1/2/4/8-byte accesses, so
 * subobjects are never overread while passing to/from GNU compiled code. */
bool minic_x86_64_one_gp_record(const MinicC0Program *program,
                              const MinicTargetInfo *target, MinicType type,
                              size_t *record_bytes) {
    MinicAggregateStorageShape shape;
    size_t size, align;
    if (program == NULL || target == NULL || record_bytes == NULL ||
        !minic_type_is_record(type)) return false;
    if (!x86_one_gp_integer_fields(program, target, type, 0U) ||
        !minic_common_aggregate_storage_shape(
            program, target->data_layout, type, 8U, &shape))
        return false;
    size = shape.bytes;
    align = shape.alignment;
    if ((size != 1U && size != 2U && size != 4U && size != 8U) ||
        align > 8U) return false;
    *record_bytes = size;
    return true;
}

/* A two-eightbyte INTEGER aggregate can contain nested unions and bitfields.
 * Reject every noninteger leaf and every genuinely unaligned scalar member,
 * while retaining the actual record offsets supplied by DataLayout. Linux's
 * sockptr_t is a 16-byte struct whose first field is an anonymous union of
 * pointers and whose final field is a one-bit boolean at byte eight. GCC
 * classifies both halves INTEGER (RDI, RSI), not a memory-passed struct. */
static bool x86_two_gp_integer_fields(const MinicC0Program *program,
    const MinicTargetInfo *target, MinicType type, size_t absolute,
    unsigned int depth) {
    const MinicRecord *rec;
    MinicType plain;
    size_t bytes, align, i;
    if (!minic_type_unqualified(type, &plain) ||
        !minic_data_layout_type(target->data_layout, program, plain,
                                &bytes, &align) || !bytes || !align ||
        bytes > 16U || absolute > 16U - bytes || depth > 12U)
        return false;
    if (minic_type_is_integer(plain) || minic_type_is_pointer(plain))
        return align <= 8U && absolute % align == 0U;
    if (!minic_type_is_record(plain)) return false;
    rec = minic_c0_program_record(program, plain.record_id);
    if (!rec || !rec->is_complete || !rec->field_count || align > 8U)
        return false;
    for (i = 0U; i < rec->field_count; ++i) {
        const MinicRecordField *field = minic_c0_record_field(rec, i);
        size_t offset, bit_offset, field_bytes, field_align;
        if (!field || field->is_array || field->is_flexible_array ||
            field->is_zero_length_array || field->element_count != 1U ||
            !minic_data_layout_type(target->data_layout, program, field->type,
                                    &field_bytes, &field_align) ||
            !minic_data_layout_record_field_layout(target->data_layout, program,
                                                    rec, i, &offset, &bit_offset) ||
            offset > bytes || absolute + offset > 16U || !field_align)
            return false;
        if (field->is_bit_field) {
            size_t start_bits, end_bits;
            if (!minic_type_is_integer(field->type) ||
                bit_offset >= 8U || field->bit_width > 64U ||
                offset >= bytes || field->bit_width > (bytes - offset) * 8U - bit_offset)
                return false;
            start_bits = (absolute + offset) * 8U + bit_offset;
            end_bits = start_bits + field->bit_width;
            /* A nonzero bitfield may not straddle a SysV eightbyte.
             * Zero-width fields carry alignment semantics but no bits. */
            if (field->bit_width &&
                (end_bits > 128U || (start_bits / 64U) != ((end_bits - 1U) / 64U)))
                return false;
        } else if (!x86_two_gp_integer_fields(program, target, field->type,
                                               absolute + offset, depth + 1U))
            return false;
    }
    return true;
}

bool minic_x86_64_two_gp_record(const MinicC0Program *program,
                              const MinicTargetInfo *target, MinicType type) {
    MinicAggregateStorageShape shape;
    if (program == NULL || target == NULL || !minic_type_is_record(type))
        return false;
    return minic_common_aggregate_storage_shape(
               program, target->data_layout, type, 8U, &shape) &&
           shape.bytes == 16U && shape.alignment == 8U &&
           x86_two_gp_integer_fields(program, target, type, 0U, 0U);
}

