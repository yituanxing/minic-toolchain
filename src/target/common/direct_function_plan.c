#include "target/common/direct_function_plan.h"
#include "target/common/direct_preflight.h"
#include "target/common/core_frame_layout.h"
#include "target/common/lp64_scalar.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* One checked preparation sequence consumed by both LP64 direct backends.
 * The target supplies actual ABI classification and makes final choices
 * about native prologue, varargs register stores and instruction emission.
 * Successful planning never writes any assembly. The caller must supply a
 * fresh/unowned plan: initialization clears it, and even a false result may
 * own partial allocations that the caller must destroy. */
bool minic_multi_direct_lp64_function_plan_prepare(
    const MinicCoreModuleFunctionSymbol *module_decl,
    const MinicCoreFunction *fn,
    const MinicCoreFunctionFacts *facts,
    const MinicCoreObjectStoragePlan *storage,
    const char *architecture_name, const MinicDirectLP64AbiPolicy *abi,
    MinicDirectLP64OpaqueAsmQualifier qualify_opaque_asm,
    MinicDirectLP64FunctionPlan *plan) {
    MinicDirectLP64FunctionProfile profile;
    MinicDirectLP64ProfileResult profile_result;
    MinicMachineLP64FrameContract *machine_frame;
    const char *name;
    size_t object_end, ld_base;
    if (plan == NULL)
        return false;
    (void)memset(plan, 0, sizeof(*plan));
    machine_frame = &plan->machine_frame;
    plan->failed_stage = "function_preflight";
    plan->failed_block = SIZE_MAX;
    plan->failed_instruction = SIZE_MAX;
    if (module_decl == NULL || fn == NULL || facts == NULL ||
        architecture_name == NULL || fn->type_context == NULL ||
        !minic_direct_lp64_abi_policy_valid(abi))
        return false;
    name = module_decl->name;
    if (!minic_multi_direct_lp64_function_preflight(
            module_decl, fn, facts, architecture_name, name,
            qualify_opaque_asm, abi))
        return false;
    profile_result = minic_multi_direct_lp64_profile_function(
        fn, facts, abi, &plan->incoming_plan, &profile);
    if (profile_result != MINIC_DIRECT_LP64_PROFILE_OK) {
        plan->failed_stage =
            profile_result == MINIC_DIRECT_LP64_PROFILE_VARIADIC_FIXED_FLOAT
                ? "variadic_fp_fixed" :
            profile_result == MINIC_DIRECT_LP64_PROFILE_VARIADIC_SHAPE
                ? "variadic_preflight" : "function_preflight";
        return false;
    }
    machine_frame->variadic_count = profile.variadic_address_count;
    machine_frame->variadic_gp_count = profile.fixed_gp_count;
    machine_frame->variadic_fp_count = profile.fixed_fp_count;
    machine_frame->has_dynamic_stack = profile.has_dynamic_stack;

    plan->failed_stage = "cfg_preflight";
    if (!minic_multi_direct_lp64_cfg_preflight(
            fn, &plan->failed_block, abi))
        return false;

    plan->failed_stage = "frame_layout";
    plan->failed_block = SIZE_MAX;
    if (!minic_lp64_frame_plan_objects_with_storage(
            fn, storage, &plan->frame_plan)) {
        if (getenv("MINIC_M1_TRACE") != NULL)
            (void)fprintf(stderr,
                "MINIC_M8_FRAME_REJECT arch=%s function=%s values=%zu objects=%zu\n",
                architecture_name, name, fn->value_count, fn->object_count);
        return false;
    }
    machine_frame->frame_alignment =
        plan->frame_plan.max_object_alignment > abi->stack_alignment
            ? plan->frame_plan.max_object_alignment : abi->stack_alignment;
    /* LoongArch's current linear va_list walks directly from the saved GARs
     * into the caller's overflow area. An aligned local-frame gap breaks
     * that contiguity; do not silently give such a function a wrong cursor. */
    if (machine_frame->frame_alignment > abi->stack_alignment &&
        machine_frame->variadic_count != 0U &&
        abi->variadic_requires_public_stack_alignment) {
        plan->failed_stage = "variadic_frame_realign";
        return false;
    }
    /* A local aligned above the public 16B stack guarantee requires a
     * callee-saved fixed base and a saved unmodified entry SP. This also
     * covers genuinely dynamic stack allocations in the same function. */
    if (machine_frame->frame_alignment > abi->stack_alignment)
        machine_frame->has_dynamic_stack = true;
    object_end = plan->frame_plan.object_end;
    plan->failed_stage = "binary128_layout";
    if (!minic_core_frame_align_up(object_end, 16U, &ld_base))
        return false;
    if (!minic_lp64_frame_plan_wide(fn, ld_base, &plan->frame_plan))
        return false;
    object_end = plan->frame_plan.wide_end;

    /* Preserve the target's hidden result pointer across nested calls.
     * The target supplies the ABI return classifier, not Core or Common. */
    if (minic_lp64_core_kind(fn->type_context,
            minic_core_function_return_type(fn), MINIC_CORE_TYPE_RECORD) &&
        abi->core_indirect_return != NULL) {
        size_t result_bytes = 0U;
        if (abi->core_indirect_return(fn->type_context,
                minic_core_function_return_type(fn), &result_bytes) && result_bytes > 16U) {
            if (!minic_core_frame_reserve(&object_end, 8U, 8U,
                                          MINIC_LARGE_FRAME_MAX,
                                          &machine_frame->indirect_return_offset))
                return false;
            machine_frame->has_indirect_return = true;
        }
    }

    /* One checked CALL traversal performs admission and ABI placement.
     * Preserve exact failing-instruction and original diagnostic stages. */
    {
        bool failed_preflight = true;
        plan->failed_stage = "call_preflight";
        if (!minic_lp64_plan_function_calls(
                fn, facts->call_index, abi, architecture_name, name,
                &plan->failed_instruction, &failed_preflight,
                &machine_frame->has_calls, &plan->outgoing_calls)) {
            plan->failed_stage =
                failed_preflight ? "call_preflight" : "call_planning";
            return false;
        }
    }

    plan->failed_stage = "frame_finalization";
    plan->failed_instruction = SIZE_MAX;
    if (!minic_lp64_finalize_direct_frame(
            object_end, machine_frame->has_calls, machine_frame->has_dynamic_stack,
            machine_frame->variadic_count != 0U, machine_frame->frame_alignment, abi,
            &machine_frame->save_offset, &machine_frame->variadic_save_base,
            &machine_frame->frame_size)) {
        if (getenv("MINIC_M1_TRACE") != NULL &&
            machine_frame->frame_size > MINIC_LARGE_FRAME_MAX)
            (void)fprintf(stderr,
                "MINIC_M8_FRAME_CAP arch=%s function=%s frame=%zu end=%zu\n",
                architecture_name, name, machine_frame->frame_size, object_end);
        return false;
    }
    machine_frame->saved_entry_sp_offset = machine_frame->frame_alignment > abi->stack_alignment
        ? machine_frame->save_offset + (machine_frame->has_calls ? 8U : 0U) + 8U
        : SIZE_MAX;
    /* AAPCS64 cursor0 keeps the established 32B tag + 64B GP + 128B FP
     * region. Each further va_copy/va_start cursor owns another 32B tag,
     * never sharing the mutable offsets of a preceding va_list. Increase
     * the final frame (not the incoming save/return offsets), so the extra
     * tags remain below entry SP and above all existing frame slots. */
    if (abi->extra_variadic_cursor_bytes != 0U &&
        machine_frame->variadic_count > 1U) {
        const size_t extra_cursors = machine_frame->variadic_count - 1U;
        if (extra_cursors > MINIC_LARGE_FRAME_MAX /
                abi->extra_variadic_cursor_bytes ||
            machine_frame->frame_size > MINIC_LARGE_FRAME_MAX -
                extra_cursors * abi->extra_variadic_cursor_bytes)
            return false;
        machine_frame->frame_size +=
            extra_cursors * abi->extra_variadic_cursor_bytes;
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
