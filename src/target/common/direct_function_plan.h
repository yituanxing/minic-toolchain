#ifndef MINIC_TARGET_COMMON_DIRECT_FUNCTION_PLAN_H
#define MINIC_TARGET_COMMON_DIRECT_FUNCTION_PLAN_H

#include "target/common/direct_function_profile.h"
#include "target/common/direct_preflight.h"
#include "target/common/lp64_frame.h"

/* The downstream physical ABI preparation dataflow for AAPCS64 and LP64D.
 * Common CoreFunctionPlan has already verified and indexed the Core once;
 * this specialized ABI family computes only supported LP64 call/frame data.
 * SysV x86 and RISC-V ILP32/LP64 deliberately use other target ABI plans.
 * No target instructions, machine register assignments or universal psABI
 * live here: every target still supplies its ABI classifier and emitter. */
typedef struct MinicDirectLP64FunctionPlan {
    MinicLP64ArgumentPlan incoming_plan;
    MinicLP64FramePlan frame_plan;
    MinicLP64CallPlanCache outgoing_calls;
    size_t frame_size;
    size_t save_offset;
    size_t indirect_return_offset;
    bool has_indirect_return;
    size_t variadic_save_base;
    size_t variadic_count;
    size_t variadic_gp_count;
    size_t variadic_fp_count;
    bool has_calls;
    bool has_dynamic_stack;
    /* Fail-closed diagnostic context, owned by the planning pass. */
    const char *failed_stage;
    size_t failed_block;
    size_t failed_instruction;
} MinicDirectLP64FunctionPlan;

bool minic_multi_direct_lp64_function_plan_prepare(
    const MinicC0Program *, const MinicTargetInfo *,
    const MinicFunction *, const MinicCoreFunction *,
    const MinicCoreFunctionFacts *,
    const MinicCoreObjectStoragePlan *, const char *architecture_name, MinicLP64DirectABI,
    MinicDirectLP64IndirectRecord,
    MinicDirectLP64IndirectRecord,
    MinicDirectLP64OpaqueAsmQualifier,
    MinicDirectLP64FunctionPlan *);
void minic_multi_direct_lp64_function_plan_destroy(
    MinicDirectLP64FunctionPlan *);

#endif
