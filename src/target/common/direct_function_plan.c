#include "target/common/direct_function_plan.h"
#include "target/common/direct_preflight.h"
#include "target/common/core_frame_layout.h"

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
    const MinicCoreFunctionFacts *facts,
    const MinicCoreObjectStoragePlan *storage,
    const char *architecture_name, MinicLP64DirectABI abi,
    MinicDirectLP64IndirectRecord indirect_record,
    MinicDirectLP64IndirectRecord indirect_return,
    MinicDirectLP64OpaqueAsmQualifier qualify_opaque_asm,
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
    if (program == NULL || target == NULL || decl == NULL || fn == NULL || facts == NULL ||
        architecture_name == NULL || indirect_record == NULL)
        return false;
    name = minic_c0_function_symbol_name(decl);
    if (!minic_multi_direct_lp64_function_preflight(
            program, target, decl, fn, facts, architecture_name, name,
            qualify_opaque_asm, indirect_return))
        return false;
    profile_result = minic_multi_direct_lp64_profile_function(
        program, target, decl, fn, facts, abi, indirect_record,
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
            program, target, fn, &plan->failed_block, indirect_return))
        return false;

    plan->failed_stage = "frame_layout";
    plan->failed_block = SIZE_MAX;
    if (!minic_lp64_frame_plan_objects_with_storage(
            program, target, fn, storage, &plan->frame_plan)) {
        if (getenv("MINIC_M1_TRACE") != NULL)
            (void)fprintf(stderr,
                "MINIC_M8_FRAME_REJECT arch=%s function=%s values=%zu objects=%zu\n",
                architecture_name, name, fn->value_count, fn->object_count);
        return false;
    }
    object_end = plan->frame_plan.object_end;
    plan->failed_stage = "binary128_layout";
    if (!minic_core_frame_align_up(object_end, 16U, &ld_base))
        return false;
    if (!minic_lp64_frame_plan_wide(fn, ld_base, &plan->frame_plan))
        return false;
    object_end = plan->frame_plan.wide_end;

    /* Preserve the target's hidden result pointer across nested calls.
     * The target supplies the ABI return classifier, not Core or Common. */
    if (indirect_return != NULL &&
        minic_type_is_record(fn->return_type)) {
        size_t result_bytes = 0U;
        if (indirect_return(program, target, fn->return_type,
                            &result_bytes) && result_bytes > 16U) {
            if (!minic_core_frame_reserve(&object_end, 8U, 8U,
                                          MINIC_LARGE_FRAME_MAX,
                                          &plan->indirect_return_offset))
                return false;
            plan->has_indirect_return = true;
        }
    }

    /* One checked CALL traversal performs admission and ABI placement.
     * Preserve exact failing-instruction and original diagnostic stages. */
    {
        bool failed_preflight = true;
        plan->failed_stage = "call_preflight";
        if (!minic_lp64_plan_function_calls(
                program, target, fn, facts->call_index, abi,
                indirect_record, indirect_return, architecture_name, name,
                &plan->failed_instruction, &failed_preflight,
                &plan->has_calls, &plan->outgoing_calls)) {
            plan->failed_stage =
                failed_preflight ? "call_preflight" : "call_planning";
            return false;
        }
    }

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
    minic_lp64_call_plan_cache_destroy(&plan->outgoing_calls);
    minic_lp64_frame_plan_destroy(&plan->frame_plan);
    (void)memset(plan, 0, sizeof(*plan));
}
