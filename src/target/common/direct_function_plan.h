#ifndef MINIC_TARGET_COMMON_DIRECT_FUNCTION_PLAN_H
#define MINIC_TARGET_COMMON_DIRECT_FUNCTION_PLAN_H

#include "target/common/direct_function_profile.h"
#include "target/common/lp64_frame.h"

/* The common seven-stage preparation dataflow for AAPCS64 and LP64D.
 * No target instructions, machine register assignments or universal psABI
 * live here: every target still supplies its ABI classifier and emitter. */
typedef struct MinicDirectLP64FunctionPlan {
    MinicLP64ArgumentPlan incoming_plan;
    MinicLP64FramePlan frame_plan;
    size_t frame_size;
    size_t save_offset;
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
    const char *architecture_name, MinicLP64DirectABI,
    MinicDirectLP64IndirectRecord,
    MinicDirectLP64FunctionPlan *);
void minic_multi_direct_lp64_function_plan_destroy(
    MinicDirectLP64FunctionPlan *);

#endif
