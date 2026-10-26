#ifndef MINIC_TARGET_COMMON_CORE_VALUE_SLOTS_H
#define MINIC_TARGET_COMMON_CORE_VALUE_SLOTS_H

#include "core/core_ir.h"
#include <stdbool.h>
#include <stddef.h>

/* Machine-neutral Core SSA identity -> stack byte placement.
 *
 * LINEAR: SysV x86-64 uses 8B scalar / 16B wide slots whose offset denotes
 * the *end* of the slot below rbp. No register/stack direction is selected.
 * WIDE_TAIL: Direct AAPCS64/LP64D use fixed 8B Core slots, then 16B sidecar
 * slots for only 128-bit integer/long-double values. The sidecar offset is
 * its *start*, and SIZE_MAX means there is no sidecar.
 *
 * RV32/RV64 keep their independently proven block-local last-use allocation
 * through core_spill_layout; no ISA-specific policy enters this allocator.
 */
typedef enum MinicCoreValueSlotPolicy {
    MINIC_CORE_VALUE_SLOT_LINEAR,
    MINIC_CORE_VALUE_SLOT_WIDE_TAIL
} MinicCoreValueSlotPolicy;

/* Allocate one function-owned value-id-indexed array. On failure, both
 * outputs are unchanged. The caller owns/free()s the returned array.
 * Bounds are inclusive for the final cursor value. */
bool minic_core_value_slots_build(
    const MinicCoreFunction *, MinicCoreValueSlotPolicy policy,
    size_t initial_bytes, size_t maximum_bytes,
    size_t **result_offsets, size_t *result_end);

#endif
