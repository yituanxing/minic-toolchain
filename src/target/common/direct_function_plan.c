#include "target/common/direct_function_plan.h"
#include "target/common/direct_preflight.h"
#include "target/common/direct_call_preflight.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* One checked preparation sequence consumed by both LP64 direct backends.
 * The target supplies actual ABI classification and makes final choices
 * about native prologue, varargs register stores and instruction emission.
 * Successful planning never writes any assembly. */
bool minic_multi_direct_lp64_function_plan_prepare(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicFunction *decl, const MinicCoreFunction *fn,
    const char *architecture_name, MinicLP64DirectABI abi,
    MinicDirectLP64IndirectRecord indirect_record,
    MinicDirectLP64FunctionPlan *plan) {
    MinicDirectLP64FunctionProfile profile;
    MinicDirectLP64ProfileResult profile_result;
    const char *name;
    size_t object_end, ld_base;
    if (plan == NULL)
        return false;
    (void)memset(plan, 0, sizeof(*plan));
    plan->failed_stage = "function_preflight";
    plan->failed_block = SIZE_MAX;
    plan->failed_instruction = SIZE_MAX;
    if (program == NULL || target == NULL || decl == NULL || fn == NULL ||
        architecture_name == NULL || indirect_record == NULL)
        return false;
    name = minic_c0_function_symbol_name(decl);
    if (!minic_multi_direct_lp64_function_preflight(
            program, target, decl, fn, architecture_name, name))
        return false;
    profile_result = minic_multi_direct_lp64_profile_function(
        program, target, decl, fn, abi, indirect_record,
        &plan->incoming_plan, &profile);
    if (profile_result != MINIC_DIRECT_LP64_PROFILE_OK) {
        plan->failed_stage =
            profile_result == MINIC_DIRECT_LP64_PROFILE_VARIADIC_FIXED_FLOAT
                ? "variadic_fp_fixed" :
            profile_result == MINIC_DIRECT_LP64_PROFILE_VARIADIC_SHAPE
                ? "variadic_preflight" : "function_preflight";
        return false;
    }
    plan->variadic_count = profile.variadic_address_count;
    plan->variadic_gp_count = profile.fixed_gp_count;
    plan->variadic_fp_count = profile.fixed_fp_count;
    plan->has_dynamic_stack = profile.has_dynamic_stack;

    plan->failed_stage = "cfg_preflight";
    if (!minic_multi_direct_lp64_cfg_preflight(
            program, target, fn, &plan->failed_block))
        return false;

    plan->failed_stage = "frame_layout";
    plan->failed_block = SIZE_MAX;
    if (!minic_lp64_frame_plan_objects(
            program, target, fn, &plan->frame_plan)) {
        if (getenv("MINIC_M1_TRACE") != NULL)
            (void)fprintf(stderr,
                "MINIC_M8_FRAME_REJECT arch=%s function=%s values=%zu objects=%zu\n",
                architecture_name, name, fn->value_count, fn->object_count);
        return false;
    }
    object_end = plan->frame_plan.object_end;
    plan->failed_stage = "binary128_layout";
    if (object_end > SIZE_MAX - 15U)
        return false;
    ld_base = (object_end + 15U) & ~(size_t)15U;
    if (!minic_lp64_frame_plan_wide(fn, ld_base, &plan->frame_plan))
        return false;
    object_end = plan->frame_plan.wide_end;

    plan->failed_stage = "call_preflight";
    if (!minic_multi_direct_lp64_calls_preflight(
            program, target, fn, architecture_name, name, indirect_record,
            &plan->failed_instruction, &plan->has_calls))
        return false;

    plan->failed_stage = "frame_finalization";
    plan->failed_instruction = SIZE_MAX;
    if (!minic_lp64_finalize_direct_frame(
            object_end, plan->has_calls, plan->has_dynamic_stack,
            plan->variadic_count != 0U, abi,
            &plan->save_offset, &plan->variadic_save_base,
            &plan->frame_size)) {
        if (getenv("MINIC_M1_TRACE") != NULL &&
            plan->frame_size > MINIC_LARGE_FRAME_MAX)
            (void)fprintf(stderr,
                "MINIC_M8_FRAME_CAP arch=%s function=%s frame=%zu end=%zu\n",
                architecture_name, name, plan->frame_size, object_end);
        return false;
    }
    plan->failed_stage = NULL;
    return true;
}

void minic_multi_direct_lp64_function_plan_destroy(
    MinicDirectLP64FunctionPlan *plan) {
    if (plan == NULL)
        return;
    minic_lp64_frame_plan_destroy(&plan->frame_plan);
    (void)memset(plan, 0, sizeof(*plan));
}
