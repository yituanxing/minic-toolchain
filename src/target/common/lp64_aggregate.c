#include "target/common/aggregate_shape.h"
#include "target/common/lp64_aggregate.h"
#include <stdint.h>

/* Production CoreType predicates below consume the immutable ABI shape
 * captured at frontend ingress. The C-facing predicates remain for isolated
 * legacy fixtures; target code with a bound CoreTypeContext must not retry
 * them after the Core classifier rejects a shape. */
static const MinicCoreType *core_aggregate_base(
    const MinicCoreTypeContext *types, MinicCoreTypeId type_id) {
    if (types == NULL) return NULL;
    return minic_core_type_get(types, minic_core_type_unqualified(types, type_id));
}

static bool core_integer_record_members(const MinicCoreTypeContext *types,
                                        MinicCoreTypeId type_id, unsigned depth) {
    const MinicCoreType *storage = minic_core_type_get(types, type_id);
    const MinicCoreType *type = core_aggregate_base(types, type_id);
    size_t i;
    if (storage == NULL || type == NULL || !storage->has_layout ||
        depth > 16U || storage->alignment == 0U || storage->alignment > 8U)
        return false;
    if (storage->size == 0U)
        return type->kind == MINIC_CORE_TYPE_RECORD &&
               type->value.record.is_complete;
    if (type->kind == MINIC_CORE_TYPE_INTEGER ||
        type->kind == MINIC_CORE_TYPE_POINTER)
        return storage->size <= 8U;
    if (type->kind != MINIC_CORE_TYPE_RECORD ||
        !type->value.record.is_complete || !type->has_layout ||
        (type->value.record.field_count != 0U &&
         type->value.record.fields == NULL))
        return false;
    if (type->value.record.field_count == 0U)
        return false; /* Nonempty record with no fields is malformed. */
    for (i = 0U; i < type->value.record.field_count; ++i) {
        const MinicCoreRecordField *field = &type->value.record.fields[i];
        const MinicCoreType *field_storage =
            minic_core_type_get(types, field->type_id);
        const MinicCoreType *field_type =
            core_aggregate_base(types, field->type_id);
        MinicCoreTypeId element_id = field->type_id;
        const MinicCoreType *element;
        bool is_array;
        size_t field_size, field_align, count = 1U;
        if (field_storage == NULL || field_type == NULL ||
            field->is_flexible_array || field->is_zero_length_array)
            return false;
        is_array = field_type->kind == MINIC_CORE_TYPE_ARRAY;
        if (is_array) {
            if (!field_type->value.array.is_complete ||
                field_type->value.array.is_zero_length ||
                field_type->value.array.count == 0U)
                return false;
            element_id = field_type->value.array.element;
            count = field_type->value.array.count;
        }
        element = minic_core_type_get(types, element_id);
        if (element == NULL || !element->has_layout ||
            element->alignment == 0U ||
            (element->size != 0U && count > SIZE_MAX / element->size))
            return false;
        field_size = element->size * count;
        field_align = element->alignment;
        if ((field->is_bit_field &&
             (is_array || field->bit_width == 0U ||
              core_aggregate_base(types, element_id)->kind !=
                  MINIC_CORE_TYPE_INTEGER)) ||
            !core_integer_record_members(types, element_id, depth + 1U) ||
            (field->is_bit_field &&
             (field_size > SIZE_MAX / 8U ||
              field->bit_width > field_size * 8U)) ||
            (!field->is_bit_field && field->byte_offset % field_align != 0U) ||
            field->byte_offset > storage->size ||
            (field_size != 0U && field->byte_offset == storage->size) ||
            (!field->is_bit_field && field_size > storage->size - field->byte_offset))
            return false;
    }
    return true;
}

bool minic_multi_gp_record_core(const MinicCoreTypeContext *types,
                                MinicCoreTypeId type_id,
                                size_t *bytes, size_t *eightbytes) {
    const MinicCoreType *type = core_aggregate_base(types, type_id);
    MinicAggregateStorageShape shape;
    if (bytes == NULL || eightbytes == NULL || type == NULL ||
        type->kind != MINIC_CORE_TYPE_RECORD ||
        !core_integer_record_members(types, type_id, 0U) ||
        !minic_common_aggregate_storage_shape_core(types, type_id, 8U, &shape) ||
        shape.bytes == 0U || shape.bytes > 16U || shape.alignment > 8U)
        return false;
    *bytes = shape.bytes;
    *eightbytes = shape.words;
    return true;
}

bool minic_multi_empty_record_core(const MinicCoreTypeContext *types,
                                   MinicCoreTypeId type_id) {
    MinicAggregateStorageShape shape;
    return minic_common_aggregate_storage_shape_core(types, type_id, 8U, &shape) &&
           shape.bytes == 0U;
}

bool minic_multi_indirect_record_core(const MinicCoreTypeContext *types,
                                      MinicCoreTypeId type_id, size_t *bytes) {
    MinicAggregateStorageShape shape;
    if (bytes == NULL ||
        !minic_common_aggregate_storage_shape_core(types, type_id, 8U, &shape) ||
        shape.bytes <= 16U || shape.bytes > 2048U || shape.alignment > 16U)
        return false;
    *bytes = shape.bytes;
    return true;
}

bool minic_multi_fp_return_record_core(
    const MinicCoreTypeContext *types, MinicCoreTypeId type_id,
    size_t *element_count, size_t *element_width) {
    const MinicCoreType *record = core_aggregate_base(types, type_id);
    const MinicCoreRecordField *first;
    const MinicCoreType *first_type;
    MinicAggregateStorageShape shape;
    size_t width, i;
    if (types == NULL || element_count == NULL || element_width == NULL ||
        record == NULL || record->kind != MINIC_CORE_TYPE_RECORD ||
        !record->value.record.is_complete || record->value.record.is_union ||
        record->value.record.is_packed ||
        record->value.record.pack_alignment != 0U ||
        record->value.record.explicit_alignment != 0U ||
        record->value.record.field_count == 0U ||
        record->value.record.field_count > 2U ||
        record->value.record.fields == NULL)
        return false;
    first = &record->value.record.fields[0];
    first_type = core_aggregate_base(types, first->type_id);
    if (first_type == NULL || first_type->kind != MINIC_CORE_TYPE_FLOAT ||
        first->is_bit_field || first->is_packed || first->is_flexible_array ||
        first->is_zero_length_array ||
        (first_type->value.floating.format != MINIC_CORE_FLOAT_BINARY32 &&
         first_type->value.floating.format != MINIC_CORE_FLOAT_BINARY64))
        return false;
    width = first_type->value.floating.format == MINIC_CORE_FLOAT_BINARY32 ? 4U : 8U;
    if (!minic_common_aggregate_storage_shape_core(types, type_id, 8U, &shape) ||
        shape.bytes != width * record->value.record.field_count ||
        shape.alignment != width)
        return false;
    for (i = 0U; i < record->value.record.field_count; ++i) {
        const MinicCoreRecordField *field = &record->value.record.fields[i];
        const MinicCoreType *field_type = core_aggregate_base(types, field->type_id);
        if (field_type == NULL || field_type->kind != MINIC_CORE_TYPE_FLOAT ||
            field->is_bit_field || field->is_packed || field->is_flexible_array ||
            field->is_zero_length_array ||
            field->type_id != first->type_id ||
            field->byte_offset != i * width)
            return false;
    }
    *element_count = record->value.record.field_count;
    *element_width = width;
    return true;
}

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
        alignment == 0U || alignment > 8U)
        return false;
    /* Empty GNU records have no ABI payload. Nested empty members do not
     * invalidate the integer class of their nonempty containing record. */
    if (total == 0U) {
        record = minic_type_is_record(type)
            ? minic_c0_program_record(program, type.record_id) : NULL;
        return record != NULL && record->is_complete;
    }
    if (minic_type_is_integer(type) || minic_type_is_pointer(type))
        return total != 0U && total <= 8U;
    if (!minic_type_is_record(type)) return false;
    record = minic_c0_program_record(program, type.record_id);
    if (record == NULL || !record->is_complete)
        return false;
    /* GNU C permits zero-sized complete records nested in an otherwise
     * nonempty aggregate.  They consume no INTEGER eightbyte and must not
     * make the enclosing record's ABI classification fail.  The public
     * GP-record predicate below still rejects a top-level zero-byte value. */
    if (record->field_count == 0U)
        return total == 0U;
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
            offset > total ||
            (field_size != 0U && offset == total) ||
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

bool minic_multi_empty_record(const MinicC0Program *program,
                              const MinicTargetInfo *target, MinicType type) {
    const MinicRecord *record;
    size_t bytes, alignment;
    if (program == NULL || target == NULL || !minic_type_is_record(type))
        return false;
    record = minic_c0_program_record(program, type.record_id);
    return record != NULL && record->is_complete &&
        minic_data_layout_type(target->data_layout, program, type,
                               &bytes, &alignment) &&
        bytes == 0U && alignment != 0U;
}

/* LP64D and AAPCS64 pass non-HFA aggregates > 2*GRLEN (16 bytes)
 * by reference. The previous >32 threshold wrongly rejected Linux
 * 17..32-byte call arguments and parameters. AArch64 separately filters
 * all FP/HFA/vector members through its target-owned ABI classifier. */
bool minic_multi_indirect_record(const MinicC0Program *program,
                                        const MinicTargetInfo *target,
                                        MinicType type, size_t *bytes) {
    MinicAggregateStorageShape shape;
    if (program == NULL || target == NULL || bytes == NULL ||
        !minic_type_is_record(type) ||
        !minic_common_aggregate_storage_shape(
            program, target->data_layout, type, 8U, &shape) ||
        /* FIXME(MC-060): the 2048-byte threshold is an emitter limitation,
         * not an LP64 ABI bound; A64/Loong reject legal larger records. */
        shape.bytes <= 16U || shape.bytes > 2048U ||
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
