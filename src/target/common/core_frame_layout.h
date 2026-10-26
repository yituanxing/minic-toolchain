#ifndef MINIC_TARGET_COMMON_CORE_FRAME_LAYOUT_H
#define MINIC_TARGET_COMMON_CORE_FRAME_LAYOUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* All five machine targets use one overflow-safe *byte* alignment contract.
 * XLEN, saved registers, stack direction and ABI spill classification remain
 * target-owned. Non-power-of-two alignment is valid for this primitive;
 * RV32's historical power-of-two precondition is checked by its wrapper. */
static inline bool minic_core_frame_align_up(
    size_t value, size_t alignment, size_t *result) {
    size_t remainder, padding;
    if (result == NULL || alignment == 0U) return false;
    remainder = value % alignment;
    padding = remainder == 0U ? 0U : alignment - remainder;
    if (value > SIZE_MAX - padding) return false;
    *result = value + padding;
    return true;
}

/* Placement arithmetic shared by ILP32 and Direct LP64. No allocation
 * decision, object reuse eligibility or frame direction occurs here.
 * Failure leaves both the cursor and destination offset unchanged. */
static inline bool minic_core_frame_reserve(
    size_t *cursor, size_t alignment, size_t extent,
    size_t maximum_end, size_t *offset) {
    size_t aligned;
    if (cursor == NULL || offset == NULL ||
        !minic_core_frame_align_up(*cursor, alignment, &aligned) ||
        aligned > maximum_end || extent > maximum_end - aligned)
        return false;
    *offset = aligned;
    *cursor = aligned + extent;
    return true;
}

#endif /* MINIC_TARGET_COMMON_CORE_FRAME_LAYOUT_H */
