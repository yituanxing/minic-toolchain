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
