#include "target/common/aggregate_shape.h"

bool minic_common_aggregate_storage_shape_core(
    const MinicCoreTypeContext *types, MinicCoreTypeId type_id,
    size_t word_bytes, MinicAggregateStorageShape *out) {
    const MinicCoreType *record, *storage;
    MinicAggregateStorageShape shape;
    if (types == NULL || out == NULL || word_bytes == 0U ||
        (word_bytes & (word_bytes - 1U)) != 0U)
        return false;
    storage = minic_core_type_get(types, type_id);
    record = minic_core_type_get(types, minic_core_type_unqualified(types, type_id));
    if (record == NULL || record->kind != MINIC_CORE_TYPE_RECORD ||
        !record->value.record.is_complete || storage == NULL ||
        !storage->has_layout || storage->alignment == 0U ||
        (storage->alignment & (storage->alignment - 1U)) != 0U)
        return false;
    shape.bytes = storage->size;
    shape.alignment = storage->alignment;
    shape.words = shape.bytes / word_bytes +
                  (shape.bytes % word_bytes != 0U ? 1U : 0U);
    *out = shape;
    return true;
}

/* Single DataLayout-backed record shape for all five target profiles.
 * No architecture/ABI branch or physical-register knowledge belongs here. */
bool minic_common_aggregate_storage_shape(
    const MinicC0Program *program, const MinicDataLayout *layout,
    MinicType type, size_t word_bytes, MinicAggregateStorageShape *out) {
    MinicAggregateStorageShape shape;
    if (program == NULL || layout == NULL || out == NULL ||
        !minic_type_is_record(type) || word_bytes == 0U ||
        (word_bytes & (word_bytes - 1U)) != 0U ||
        !minic_data_layout_type(layout, program, type,
                                &shape.bytes, &shape.alignment) ||
        shape.alignment == 0U)
        return false;
    /* ceil(bytes/word) without overflow; GNU empty records take 0 words. */
    shape.words = shape.bytes / word_bytes +
                  (shape.bytes % word_bytes != 0U ? 1U : 0U);
    *out = shape;
    return true;
}
