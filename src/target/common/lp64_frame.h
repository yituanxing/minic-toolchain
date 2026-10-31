#ifndef MINIC_TARGET_COMMON_LP64_FRAME_H
#define MINIC_TARGET_COMMON_LP64_FRAME_H

/* Shared bounded frame contract for both direct LP64 backends. */
#define MINIC_LARGE_FRAME_MAX 1048576U

#include "core/core_ir.h"
#include "target/common/direct_lp64_abi.h"
#include "target/common/core_object_storage.h"
#include "frontend/ast.h"
#include "target/target_info.h"
#include <stdbool.h>
#include <stddef.h>

/* Preserve the historical 1 MiB input-admission ceiling. The actual scalar
 * SSA frame footprint is now computed using the already-shared Core last-use
 * layout, not an independent per-value 8-byte slot. Object storage, wide
 * tails, callee saves and outgoing calls use the same frame budget. */
static inline bool minic_lp64_baseline_values_fit_frame(size_t count) {
    return count <= MINIC_LARGE_FRAME_MAX / 8U;
}

/* Function-owned plan, computed once before emitting a direct LP64 function.
 * Like RV64 and x86-64, object/128-bit slots are indexed by Core IDs.
 * Actual ABI callee-save, varargs and outgoing call policy stays separate. */
typedef struct MinicLP64FramePlan {
    size_t object_count, value_count;
    size_t object_end, wide_base, wide_end;
    size_t max_object_alignment;
    size_t *object_offsets;
    size_t *value_offsets;
    size_t *wide_offsets;
    MinicCoreObjectStoragePlan object_storage;
    bool borrows_object_storage;
} MinicLP64FramePlan;

bool minic_lp64_frame_plan_objects_with_storage(
    const MinicC0Program *, const MinicTargetInfo *,
    const MinicCoreFunction *, const MinicCoreObjectStoragePlan *,
    MinicLP64FramePlan *);
bool minic_lp64_frame_plan_wide(
    const MinicCoreFunction *, size_t wide_base, MinicLP64FramePlan *);
bool minic_lp64_frame_plan_object(
    const MinicLP64FramePlan *, size_t object_id, size_t *offset);
bool minic_lp64_frame_plan_value_slot(
    const MinicLP64FramePlan *, MinicCoreValueId value_id, size_t *offset);
/* Machine emitters already traffic in byte-offset expressions. Invalid or
 * unallocated SSA values become a strictly out-of-frame sentinel, NEVER the
 * previous id*8 layout. Target frame transports must reject values above
 * MINIC_LARGE_FRAME_MAX; this also safely absorbs small limb additions. */
static inline size_t minic_lp64_frame_value_offset(
    const MinicLP64FramePlan *plan, MinicCoreValueId value_id) {
    size_t offset;
    return minic_lp64_frame_plan_value_slot(plan, value_id, &offset)
        ? offset : (size_t)MINIC_LARGE_FRAME_MAX + 1U;
}
bool minic_lp64_frame_plan_wide_slot(
    const MinicLP64FramePlan *, MinicCoreValueId value_id, size_t *offset);
void minic_lp64_frame_plan_destroy(MinicLP64FramePlan *);

/* Direct LP64 frame finalization; ISA instructions are never emitted here. */
bool minic_lp64_finalize_direct_frame(
    size_t object_end, bool has_calls, bool dynamic_frame, bool has_varargs,
    size_t frame_alignment, const MinicDirectLP64AbiPolicy *abi,
    size_t *save_offset, size_t *va_base,
    size_t *frame_size);

#endif
