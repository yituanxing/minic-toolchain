#include "target/common/lp64_frame.h"
#include "target/common/core_object_storage.h"
#include "target/common/core_frame_layout.h"
#include "target/common/core_value_slots.h"
#include <stdint.h>
#include <stdlib.h>

/* Bounded function-local layout, shared by AAPCS64 and LoongArch LP64D.
 * Identical offsets/guards to the historical per-access scans, but O(N)
 * once per function and O(1) on the emission hot path. */

bool minic_lp64_frame_plan_objects_with_storage(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreObjectStoragePlan *shared,
    MinicLP64FramePlan *plan) {
    size_t pos, i;
    if (program == NULL || target == NULL || fn == NULL || plan == NULL ||
        !minic_lp64_baseline_values_fit_frame(fn->value_count) ||
        fn->object_count > 8192U ||
        (fn->object_count != 0U && fn->objects == NULL) ||
        plan->object_offsets != NULL || plan->wide_offsets != NULL)
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
    pos = fn->value_count * 8U;
    plan->max_object_alignment = 1U;
    for (i = 0U; i < plan->object_storage.count; ++i) {
        const MinicCoreObjectStorage *storage = &plan->object_storage.objects[i];
        const size_t align = storage->alignment;
        const size_t extent = storage->extent;
        /* The target prologue must actually realign its stable frame base
         * when a GNU local needs more than the 16-byte public call ABI. */
        if (align > 256U || extent > MINIC_LARGE_FRAME_MAX ||
            !minic_core_frame_reserve(&pos, align, extent,
                                      MINIC_LARGE_FRAME_MAX,
                                      &plan->object_offsets[i]))
            return false;
        if (align > plan->max_object_alignment)
            plan->max_object_alignment = align;
    }
    plan->object_end = pos;
    return true;
}

bool minic_lp64_frame_plan_wide(
    const MinicCoreFunction *fn, size_t wide_base, MinicLP64FramePlan *plan) {
    size_t end;
    if (fn == NULL || plan == NULL || fn->value_count != plan->value_count ||
        wide_base > MINIC_LARGE_FRAME_MAX || plan->wide_offsets != NULL ||
        (fn->value_count != 0U && fn->values == NULL) ||
        !minic_core_value_slots_build(
            fn, MINIC_CORE_VALUE_SLOT_WIDE_TAIL, wide_base,
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
    size_t frame_alignment, MinicLP64DirectABI abi,
    size_t *save_offset, size_t *va_base,
    size_t *frame_size) {
    size_t save, required, frame, va = 0U, slack;
    if (save_offset == NULL || frame_size == NULL ||
        object_end > MINIC_LARGE_FRAME_MAX ||
        frame_alignment < 16U || frame_alignment > 256U ||
        (frame_alignment & (frame_alignment - 1U)) != 0U ||
        (frame_alignment > 16U && !dynamic_frame) ||
        (abi != MINIC_LP64_ABI_AAPCS64 &&
         abi != MINIC_LP64_ABI_LOONGARCH64_LP64D))
        return false;
    if (!minic_core_frame_align_up(object_end, 8U, &save)) return false;
    if (abi == MINIC_LP64_ABI_AAPCS64 && has_varargs) {
        if (va_base == NULL || save > MINIC_LARGE_FRAME_MAX - 240U)
            return false;
        if (!minic_core_frame_align_up(save, 16U, &va)) return false;
        save = va + 224U;
    }
    required = (has_calls ? 8U : 0U) + (dynamic_frame ? 8U : 0U) +
               (frame_alignment > 16U ? 8U : 0U);
    if (abi == MINIC_LP64_ABI_LOONGARCH64_LP64D && has_varargs)
        required += 64U;
    if (save > MINIC_LARGE_FRAME_MAX - required) return false;
    if (!minic_core_frame_align_up(save + required, 16U, &frame))
        return false;
    /* Entry SP only guarantees 16B. This slack bounds the upward rounding
     * of the target's aligned fixed-frame base while keeping SP 16B. */
    slack = frame_alignment - 16U;
    if (frame > MINIC_LARGE_FRAME_MAX ||
        slack > MINIC_LARGE_FRAME_MAX - frame) return false;
    *save_offset = save;
    if (abi == MINIC_LP64_ABI_LOONGARCH64_LP64D && has_varargs)
        va = frame - 64U;
    if (va_base != NULL) *va_base = va;
    /* Expose the actual SP subtraction, including all alignment slack. */
    *frame_size = frame + slack;
    return true;
}
