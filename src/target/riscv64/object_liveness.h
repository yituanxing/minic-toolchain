#ifndef MINIC_TARGET_RISCV64_OBJECT_LIVENESS_H
#define MINIC_TARGET_RISCV64_OBJECT_LIVENESS_H
#include "target/riscv64/object_intervals.h"
#include "core/core_ir.h"
#include "frontend/ast.h"
#include <stdbool.h>
#include <stddef.h>
bool minic_riscv64_record_call_result_snapshot_reuse_candidate(
    const MinicCoreFunction *, MinicCoreObjectId, MinicCoreObjectId *);

/* Entire conservative object slot-reuse result owned by one RV64 frame.
 * Reentrant across functions; optional indexes live and die with the plan. */
typedef struct MinicRiscv64ObjectReusePlan {
    MinicRiscv64ObjectIntervalAnalysis analysis;
    CoreScalarObjectInterval *intervals;
    size_t *slots, object_count, peak_slots;
    bool prepared, crossblock_enabled;
} MinicRiscv64ObjectReusePlan;
bool minic_riscv64_object_reuse_plan_build(
    const MinicC0Program *, const MinicCoreFunction *, bool,
    MinicRiscv64ObjectReusePlan *);
bool minic_riscv64_object_reuse_plan_get(
    const MinicRiscv64ObjectReusePlan *, MinicCoreObjectId,
    bool *, size_t *, size_t *);
void minic_riscv64_object_reuse_plan_destroy(
    MinicRiscv64ObjectReusePlan *);
#endif
