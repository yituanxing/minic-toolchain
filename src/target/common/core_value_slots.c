#include "target/common/core_value_slots.h"
#include <stdint.h>
#include <stdlib.h>

/* Only wide Direct LP64 sidecars remain here. The former linear x86 policy
 * was superseded by the one-pass Core spill allocator on all five targets. */
bool minic_core_wide_value_slots_build(
    const MinicCoreFunction *function,
    size_t initial_bytes, size_t maximum_bytes,
    size_t **result_offsets, size_t *result_end) {
    size_t *offsets = NULL;
    size_t cursor = initial_bytes;
    size_t i;
    if (function == NULL || function->type_context == NULL ||
        result_offsets == NULL || result_end == NULL ||
        *result_offsets != NULL || initial_bytes > maximum_bytes ||
        (function->value_count != 0U && function->values == NULL) ||
        function->value_count > SIZE_MAX / sizeof(*offsets))
        return false;

    if (function->value_count != 0U) {
        offsets = malloc(function->value_count * sizeof(*offsets));
        if (offsets == NULL) return false;
    }
    for (i = 0U; i < function->value_count; ++i) {
        const MinicCoreTypeId id = minic_core_type_unqualified(
            function->type_context, function->values[i].core_type_id);
        const MinicCoreType *type = minic_core_type_get(function->type_context, id);
        bool wide;
        if (type == NULL) {
            free(offsets);
            return false;
        }
        wide = (type->kind == MINIC_CORE_TYPE_INTEGER &&
                type->value.integer.bit_width == 128U) ||
               (type->kind == MINIC_CORE_TYPE_FLOAT &&
                minic_core_float_format_is_wide(type->value.floating.format)) ||
               (type->kind == MINIC_CORE_TYPE_VECTOR &&
                type->value.vector.representation ==
                    MINIC_CORE_VECTOR_REGISTER_VALUE && type->has_layout &&
                type->size == 16U);
        const size_t bytes = wide ? 16U : 0U;
        if (bytes > maximum_bytes || cursor > maximum_bytes - bytes) {
            free(offsets);
            return false;
        }
        offsets[i] = bytes == 0U ? SIZE_MAX : cursor;
        cursor += bytes;
    }
    *result_offsets = offsets;
    *result_end = cursor;
    return true;
}
