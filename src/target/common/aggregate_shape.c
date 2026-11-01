#include "target/common/aggregate_shape.h"

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
