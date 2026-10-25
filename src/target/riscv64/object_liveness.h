#ifndef MINIC_TARGET_RISCV64_OBJECT_LIVENESS_H
#define MINIC_TARGET_RISCV64_OBJECT_LIVENESS_H
#include "target/riscv64/object_intervals.h"
#include "target/common/core_object_reuse.h"
bool minic_riscv64_record_call_result_snapshot_reuse_candidate(
    const MinicCoreFunction *, MinicCoreObjectId, MinicCoreObjectId *);
typedef MinicCoreObjectReusePlan MinicRiscv64ObjectReusePlan;
bool minic_riscv64_object_reuse_plan_build(
    const MinicC0Program *, const MinicCoreFunction *, bool, MinicRiscv64ObjectReusePlan *);
bool minic_riscv64_object_reuse_plan_get(
    const MinicRiscv64ObjectReusePlan *, MinicCoreObjectId,
    bool *, size_t *, size_t *);
void minic_riscv64_object_reuse_plan_destroy(MinicRiscv64ObjectReusePlan *);
#endif
