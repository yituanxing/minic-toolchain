#include "target/common/core_value_slots.h"
#include "frontend/type.h"
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
    if (function == NULL || result_offsets == NULL || result_end == NULL ||
        *result_offsets != NULL || initial_bytes > maximum_bytes ||
        (function->value_count != 0U && function->values == NULL) ||
        function->value_count > SIZE_MAX / sizeof(*offsets))
        return false;

    if (function->value_count != 0U) {
        offsets = malloc(function->value_count * sizeof(*offsets));
        if (offsets == NULL) return false;
    }
    for (i = 0U; i < function->value_count; ++i) {
        const MinicType type = function->values[i].type;
        const bool wide = minic_type_is_int128_integer(type) ||
                          minic_type_is_long_double(type);
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
