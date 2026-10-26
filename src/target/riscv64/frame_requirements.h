#ifndef MINIC_TARGET_RISCV64_FRAME_REQUIREMENTS_H
#define MINIC_TARGET_RISCV64_FRAME_REQUIREMENTS_H
#include "core/core_ir.h"
#include "target/common/core_call_view.h"
#include "target/riscv64/abi.h"
#include "frontend/ast.h"
#include <stdbool.h>
#include <stddef.h>
/* RV64-owned psABI CALL planning, with immutable Common CALL identities. */
/* Common owns CALL identities. RV64 owns a single immutable per-call
 * psABI placement (including hidden return pointer and overflow stack).
 * The frame and machine emitter share this plan for the exact Core CALL. */
typedef struct MinicRiscv64PreparedCall {
    size_t instruction_id;
    MinicRiscv64AbiValue return_value;
    size_t argument_count;
    MinicRiscv64AbiArgumentLocation arguments[];
} MinicRiscv64PreparedCall;
typedef struct MinicRiscv64CallPlanCache {
    MinicRiscv64PreparedCall **sites; /* ordered, one per actual CALL */
    size_t count;
    size_t maximum_stack_bytes;
} MinicRiscv64CallPlanCache;
bool minic_riscv64_call_plan_cache_build(
    const MinicC0Program *, const MinicCoreFunction *, bool lp64_soft_float,
    const MinicCoreCallIndex *, MinicRiscv64CallPlanCache *);
const MinicRiscv64PreparedCall *minic_riscv64_call_plan_cache_get(
    const MinicRiscv64CallPlanCache *, size_t instruction_id);
void minic_riscv64_call_plan_cache_destroy(MinicRiscv64CallPlanCache *);

#endif
