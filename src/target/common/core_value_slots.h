#ifndef MINIC_TARGET_COMMON_CORE_VALUE_SLOTS_H
#define MINIC_TARGET_COMMON_CORE_VALUE_SLOTS_H

#include "core/core_ir.h"
#include <stdbool.h>
#include <stddef.h>

/* Direct AAPCS64/LP64D wide-value sidecar only. Ordinary SSA scalar slots
 * for all five targets use the shared block-local last-use allocator in
 * core_spill_layout. Each 128-bit integer/long-double gets a 16-byte sidecar
 * at the returned start offset; narrow values receive SIZE_MAX. */

/* Allocate one function-owned value-id-indexed array. On failure, both
 * outputs are unchanged. The caller owns/free()s the returned array.
 * Bounds are inclusive for the final cursor value. */
bool minic_core_wide_value_slots_build(
    const MinicCoreFunction *,
    size_t initial_bytes, size_t maximum_bytes,
    size_t **result_offsets, size_t *result_end);

#endif
