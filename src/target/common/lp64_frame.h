#ifndef MINIC_TARGET_COMMON_LP64_FRAME_H
#define MINIC_TARGET_COMMON_LP64_FRAME_H

#include "core/core_ir.h"
#include "target/common/direct_lp64_abi.h"
#include "frontend/ast.h"
#include "target/target_info.h"
#include <stdbool.h>
#include <stddef.h>

/* Function-owned plan, computed once before emitting a direct LP64 function.
 * Like RV64 and x86-64, object/128-bit slots are indexed by Core IDs.
 * Actual ABI callee-save, varargs and outgoing call policy stays separate. */
typedef struct MinicLP64FramePlan {
    size_t object_count, value_count;
    size_t object_end, wide_base, wide_end;
    size_t *object_offsets;
    size_t *wide_offsets;
} MinicLP64FramePlan;

bool minic_lp64_frame_plan_objects(
    const MinicC0Program *, const MinicTargetInfo *,
    const MinicCoreFunction *, MinicLP64FramePlan *);
bool minic_lp64_frame_plan_wide(
    const MinicCoreFunction *, size_t wide_base, MinicLP64FramePlan *);
bool minic_lp64_frame_plan_object(
    const MinicLP64FramePlan *, size_t object_id, size_t *offset);
bool minic_lp64_frame_plan_wide_slot(
    const MinicLP64FramePlan *, MinicCoreValueId value_id, size_t *offset);
void minic_lp64_frame_plan_destroy(MinicLP64FramePlan *);

/* Direct LP64 frame finalization; ISA instructions are never emitted here. */
bool minic_lp64_finalize_direct_frame(
    size_t object_end, bool has_calls, bool dynamic_frame, bool has_varargs,
    MinicLP64DirectABI abi, size_t *save_offset, size_t *va_base,
    size_t *frame_size);

#endif
