#include "target/common/lp64_frame.h"
#include "target/common/core_object_storage.h"
#include "target/common/core_frame_layout.h"
#include "target/common/core_value_slots.h"
#include "target/common/core_spill_layout.h"
#include "target/common/core_object_reuse.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* Bounded function-local layout, shared by AAPCS64 and LoongArch LP64D.
 * Identical offsets/guards to the historical per-access scans, but O(N)
 * once per function and O(1) on the emission hot path. */

bool minic_lp64_frame_plan_objects_with_storage(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreObjectStoragePlan *shared,
    MinicLP64FramePlan *plan) {
    size_t pos, i, scalar_bytes = 0U, pool_slots = 0U, pool_base = 0U;
    MinicCoreObjectReusePlan reuse = {0};
    if (program == NULL || target == NULL || fn == NULL || plan == NULL ||
        !minic_lp64_baseline_values_fit_frame(fn->value_count) ||
        fn->object_count > 8192U ||
        (fn->object_count != 0U && fn->objects == NULL) ||
        plan->object_offsets != NULL || plan->value_offsets != NULL ||
        plan->wide_offsets != NULL)
        return false;
    /* Shared one-time Core storage plan also serves x86 and RV64.
     * The LP64 emitters keep their exact 8-byte slots and 1 MiB frame cap. */
    if (!minic_core_object_storage_plan_acquire(
            program, target->data_layout, fn, shared,
            &plan->object_storage, &plan->borrows_object_storage))
        return false;
    if (fn->object_count != 0U) {
        plan->object_offsets =
            (size_t *)malloc(fn->object_count * sizeof(*plan->object_offsets));
        if (plan->object_offsets == NULL) return false;
    }
    plan->object_count = fn->object_count;
    plan->value_count = fn->value_count;
    if (fn->value_count != 0U) {
        plan->value_offsets = (size_t *)malloc(fn->value_count *
                                               sizeof(*plan->value_offsets));
        if (plan->value_offsets == NULL) return false;
        for (i = 0U; i < fn->value_count; ++i)
            plan->value_offsets[i] = SIZE_MAX;
    }
    if (!minic_core_function_reused_spill_layout(
            program, fn, target->data_layout, 8U, 0U,
            plan->value_offsets, &scalar_bytes, NULL))
        return false;
    /* The source Core contract defines a result for each value ID; no
     * backend may emit an id*8 fallback for an unmapped SSA value. */
    for (i = 0U; i < fn->value_count; ++i)
        if (plan->value_offsets[i] == SIZE_MAX)
            return false;
    pos = scalar_bytes;
    plan->max_object_alignment = 1U;
    /* Reuse the existing Core first-write/address-escape/interval proof used
     * by RV32/RV64. The LP64 frame owns only the resulting byte offsets;
     * the short-lived analysis cannot escape the planner. */
    if (!minic_core_object_reuse_plan_build(
            program, fn, target->data_layout, 8U, true, &reuse) ||
        !minic_core_object_reuse_plan_get(
            &reuse, MINIC_CORE_OBJECT_INVALID, NULL, NULL, &pool_slots)) {
        minic_core_object_reuse_plan_destroy(&reuse);
        return false;
    }
    for (i = 0U; i < plan->object_storage.count; ++i) {
        const MinicCoreObjectStorage *storage = &plan->object_storage.objects[i];
        const size_t align = storage->alignment;
        const size_t extent = storage->extent;
        bool reusable;
        size_t slot, peak;
        /* The target prologue must actually realign its stable frame base
         * when a GNU local needs more than the 16-byte public call ABI. */
        if (align > 256U || extent > MINIC_LARGE_FRAME_MAX ||
            !minic_core_object_reuse_plan_get(
                &reuse, (MinicCoreObjectId)i,
                &reusable, &slot, &peak) || peak != pool_slots) {
            minic_core_object_reuse_plan_destroy(&reuse);
            return false;
        }
        if (align > plan->max_object_alignment)
            plan->max_object_alignment = align;
        if (reusable) continue;
        if (!minic_core_frame_reserve(&pos, align, extent,
                                      MINIC_LARGE_FRAME_MAX,
                                      &plan->object_offsets[i])) {
            minic_core_object_reuse_plan_destroy(&reuse);
            return false;
        }
    }
    if (pool_slots > MINIC_LARGE_FRAME_MAX / 8U ||
        !minic_core_frame_reserve(&pos, 8U, pool_slots * 8U,
                                  MINIC_LARGE_FRAME_MAX, &pool_base)) {
        minic_core_object_reuse_plan_destroy(&reuse);
        return false;
    }
    for (i = 0U; i < plan->object_storage.count; ++i) {
        bool reusable;
        size_t slot, peak;
        if (!minic_core_object_reuse_plan_get(
                &reuse, (MinicCoreObjectId)i,
                &reusable, &slot, &peak) || peak != pool_slots ||
            (reusable && slot >= pool_slots)) {
            minic_core_object_reuse_plan_destroy(&reuse);
            return false;
        }
        if (reusable) plan->object_offsets[i] = pool_base + slot * 8U;
    }
    minic_core_object_reuse_plan_destroy(&reuse);
    plan->object_end = pos;
    if (getenv("MINIC_CORE_FRAME_TRACE") != NULL)
        (void)fprintf(stderr,
            "CORE_FRAME_LP64 function=%s scalar_bytes=%zu object_end=%zu "
            "objects=%zu reusable_slots=%zu\n",
            fn->name != NULL ? fn->name : "?", scalar_bytes,
            pos, fn->object_count, pool_slots);
    return true;
}

bool minic_lp64_frame_plan_wide(
    const MinicCoreFunction *fn, size_t wide_base, MinicLP64FramePlan *plan) {
    size_t end;
    if (fn == NULL || plan == NULL || fn->value_count != plan->value_count ||
        wide_base > MINIC_LARGE_FRAME_MAX || plan->wide_offsets != NULL ||
        (fn->value_count != 0U && fn->values == NULL) ||
        !minic_core_wide_value_slots_build(
            fn, wide_base,
            MINIC_LARGE_FRAME_MAX, &plan->wide_offsets, &end))
        return false;
    plan->wide_base = wide_base;
    plan->wide_end = end;
    return true;
}

bool minic_lp64_frame_plan_object(
    const MinicLP64FramePlan *plan, size_t object_id, size_t *offset) {
    if (plan == NULL || offset == NULL || object_id >= plan->object_count ||
        plan->object_offsets == NULL) return false;
    *offset = plan->object_offsets[object_id];
    return true;
}

bool minic_lp64_frame_plan_value_slot(
    const MinicLP64FramePlan *plan, MinicCoreValueId value_id, size_t *offset) {
    if (plan == NULL || offset == NULL || value_id >= plan->value_count ||
        plan->value_offsets == NULL ||
        plan->value_offsets[value_id] == SIZE_MAX ||
        plan->value_offsets[value_id] > MINIC_LARGE_FRAME_MAX)
        return false;
    *offset = plan->value_offsets[value_id];
    return true;
}

bool minic_lp64_frame_plan_wide_slot(
    const MinicLP64FramePlan *plan, MinicCoreValueId value_id, size_t *offset) {
    if (plan == NULL || offset == NULL || value_id >= plan->value_count ||
        plan->wide_offsets == NULL ||
        plan->wide_offsets[value_id] == SIZE_MAX) return false;
    *offset = plan->wide_offsets[value_id];
    return true;
}

void minic_lp64_frame_plan_destroy(MinicLP64FramePlan *plan) {
    if (plan == NULL) return;
    free(plan->object_offsets);
    free(plan->value_offsets);
    free(plan->wide_offsets);
    minic_core_object_storage_plan_release(
        &plan->object_storage, plan->borrows_object_storage);
    *plan = (MinicLP64FramePlan){0};
}

/* AAPCS64 reserves a separate 224-byte varargs tag/GP/FP save region.
 * LoongArch LP64D uses 64 bytes of integer argument saves.
 * Preserve existing ABI-specific bounds, offset and stack alignment. */
bool minic_lp64_finalize_direct_frame(
    size_t object_end, bool has_calls, bool dynamic_frame, bool has_varargs,
    size_t frame_alignment, const MinicDirectLP64AbiPolicy *abi,
    size_t *save_offset, size_t *va_base,
    size_t *frame_size) {
    size_t save, required, frame, va = 0U, slack;
    if (save_offset == NULL || frame_size == NULL ||
        !minic_direct_lp64_abi_policy_valid(abi) ||
        object_end > MINIC_LARGE_FRAME_MAX ||
        frame_alignment < abi->stack_alignment || frame_alignment > 256U ||
        (frame_alignment & (frame_alignment - 1U)) != 0U ||
        (frame_alignment > abi->stack_alignment && !dynamic_frame))
        return false;
    if (!minic_core_frame_align_up(object_end,
                                    abi->stack_slot_bytes, &save)) return false;
    /* ABI-state cursors own a tag and register-save area below the physical
     * callee-save entries.  Linear cursors save GPRs at the end of the frame
     * to remain contiguous with the caller's overflow stack arguments. */
    if (abi->variadic_cursor == MINIC_DIRECT_LP64_VARIADIC_ABI_STATE &&
        has_varargs) {
        if (va_base == NULL ||
            abi->variadic_save_bytes >
                MINIC_LARGE_FRAME_MAX - abi->variadic_save_alignment ||
            save > MINIC_LARGE_FRAME_MAX - abi->variadic_save_bytes -
                abi->variadic_save_alignment)
            return false;
        if (!minic_core_frame_align_up(
                save, abi->variadic_save_alignment, &va)) return false;
        save = va + abi->variadic_save_bytes;
    }
    required = (has_calls ? 8U : 0U) + (dynamic_frame ? 8U : 0U) +
               (frame_alignment > abi->stack_alignment ? 8U : 0U);
    if (abi->variadic_cursor == MINIC_DIRECT_LP64_VARIADIC_LINEAR_GP &&
        has_varargs)
        required += abi->variadic_save_bytes;
    if (save > MINIC_LARGE_FRAME_MAX - required) return false;
    if (!minic_core_frame_align_up(
            save + required, abi->stack_alignment, &frame))
        return false;
    /* Entry SP only guarantees 16B. This slack bounds the upward rounding
     * of the target's aligned fixed-frame base while keeping SP 16B. */
    slack = frame_alignment - abi->stack_alignment;
    if (frame > MINIC_LARGE_FRAME_MAX ||
        slack > MINIC_LARGE_FRAME_MAX - frame) return false;
    *save_offset = save;
    if (abi->variadic_cursor == MINIC_DIRECT_LP64_VARIADIC_LINEAR_GP &&
        has_varargs)
        va = frame - abi->variadic_save_bytes;
    if (va_base != NULL) *va_base = va;
    /* Expose the actual SP subtraction, including all alignment slack. */
    *frame_size = frame + slack;
    return true;
}
