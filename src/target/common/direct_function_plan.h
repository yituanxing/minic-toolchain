#ifndef MINIC_TARGET_COMMON_DIRECT_FUNCTION_PLAN_H
#define MINIC_TARGET_COMMON_DIRECT_FUNCTION_PLAN_H

#include "target/common/direct_function_profile.h"
#include "target/common/direct_preflight.h"
#include "target/common/lp64_frame.h"
#include "target/common/machine_lp64_frame.h"

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
    MinicMachineLP64FrameContract machine_frame;
    /* Fail-closed diagnostic context, owned by the planning pass. */
    const char *failed_stage;
    size_t failed_block;
    size_t failed_instruction;
} MinicDirectLP64FunctionPlan;

bool minic_multi_direct_lp64_function_plan_prepare(
    const MinicCoreModuleFunctionSymbol *,
    const MinicCoreFunction *,
    const MinicCoreFunctionFacts *,
    const MinicCoreObjectStoragePlan *, const char *architecture_name,
    const MinicDirectLP64AbiPolicy *,
    MinicDirectLP64OpaqueAsmQualifier,
    MinicDirectLP64FunctionPlan *);
void minic_multi_direct_lp64_function_plan_destroy(
    MinicDirectLP64FunctionPlan *);

#endif
