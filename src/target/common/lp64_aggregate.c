#include "target/common/aggregate_shape.h"
#include "target/common/lp64_aggregate.h"
#include <stdint.h>

/* LP64 record ABI admission consumes the immutable Core shape captured at
 * source ingress. Reinterpreting the C record tree inside Target would form
 * a second, potentially divergent owner of field layout and HFA classes. */
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
        if (field_storage == NULL || field_type == NULL)
            return false;
        is_array = field_type->kind == MINIC_CORE_TYPE_ARRAY;
        if (is_array) {
            if (!field_type->value.array.is_complete ||
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
        record->value.record.has_nondefault_layout_policy ||
        record->value.record.field_count == 0U ||
        record->value.record.field_count > 2U ||
        record->value.record.fields == NULL)
        return false;
    first = &record->value.record.fields[0];
    first_type = core_aggregate_base(types, first->type_id);
    if (first_type == NULL || first_type->kind != MINIC_CORE_TYPE_FLOAT ||
        first->is_bit_field || first->has_nondefault_layout_policy ||
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
            field->is_bit_field || field->has_nondefault_layout_policy ||
            field->type_id != first->type_id ||
            field->byte_offset != i * width)
            return false;
    }
    *element_count = record->value.record.field_count;
    *element_width = width;
    return true;
}
