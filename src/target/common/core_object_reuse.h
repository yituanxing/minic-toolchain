#ifndef MINIC_TARGET_COMMON_CORE_OBJECT_REUSE_H
#define MINIC_TARGET_COMMON_CORE_OBJECT_REUSE_H
#include "target/common/core_object_intervals.h"
#include "core/core_ir.h"
#include "frontend/ast.h"
#include <stdbool.h>
#include <stddef.h>
/* Entire conservative object slot-reuse result owned by one RV64 frame.
 * Reentrant across functions; optional indexes live and die with the plan. */
typedef struct MinicCoreObjectReusePlan {
    MinicCoreObjectIntervalAnalysis analysis;
    CoreScalarObjectInterval *intervals;
    size_t *slots, object_count, peak_slots;
    bool prepared, crossblock_enabled;
} MinicCoreObjectReusePlan;
bool minic_core_object_reuse_plan_build(
    const MinicC0Program *, const MinicCoreFunction *,
    const MinicDataLayout *, size_t unit_bytes, bool,
    MinicCoreObjectReusePlan *);
bool minic_core_object_reuse_plan_get(
    const MinicCoreObjectReusePlan *, MinicCoreObjectId,
    bool *, size_t *, size_t *);
void minic_core_object_reuse_plan_destroy(
    MinicCoreObjectReusePlan *);
#endif
