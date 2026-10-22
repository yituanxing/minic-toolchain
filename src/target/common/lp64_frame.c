#include "target/common/lp64_frame.h"
#include <stdint.h>
#include <stdlib.h>

/* Bounded function-local layout, shared by AAPCS64 and LoongArch LP64D.
 * Identical offsets/guards to the historical per-access scans, but O(N)
 * once per function and O(1) on the emission hot path. */

bool minic_lp64_frame_plan_objects(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, MinicLP64FramePlan *plan) {
    size_t pos, i;
    if (program == NULL || target == NULL || fn == NULL || plan == NULL ||
        fn->value_count > 32768U || fn->object_count > 8192U ||
        (fn->object_count != 0U && fn->objects == NULL) ||
        plan->object_offsets != NULL || plan->wide_offsets != NULL)
        return false;
    if (fn->object_count != 0U) {
        plan->object_offsets =
            (size_t *)malloc(fn->object_count * sizeof(*plan->object_offsets));
        if (plan->object_offsets == NULL) return false;
    }
    plan->object_count = fn->object_count;
    plan->value_count = fn->value_count;
    pos = fn->value_count * 8U;
    for (i = 0U; i < fn->object_count; ++i) {
        const MinicCoreObject *obj = &fn->objects[i];
        size_t unit, align, extent;
        if (!minic_data_layout_type(target->data_layout, program,
                                    obj->type, &unit, &align) ||
            obj->element_count == 0U ||
            unit > MINIC_LARGE_FRAME_MAX / obj->element_count)
            return false;
        extent = unit * obj->element_count;
        if (obj->explicit_alignment > align)
            align = obj->explicit_alignment;
        if (align == 0U || align > 16U || (align & (align - 1U)) != 0U)
            return false;
        if (pos > MINIC_LARGE_FRAME_MAX - (align - 1U)) return false;
        pos = (pos + align - 1U) & ~(align - 1U);
        plan->object_offsets[i] = pos;
        if (extent > MINIC_LARGE_FRAME_MAX - pos) return false;
        pos += extent;
    }
    plan->object_end = pos;
    return true;
}

bool minic_lp64_frame_plan_wide(
    const MinicCoreFunction *fn, size_t wide_base, MinicLP64FramePlan *plan) {
    size_t i, pos = wide_base;
    if (fn == NULL || plan == NULL || fn->value_count != plan->value_count ||
        wide_base > MINIC_LARGE_FRAME_MAX || plan->wide_offsets != NULL ||
        (fn->value_count != 0U && fn->values == NULL))
        return false;
    if (fn->value_count != 0U) {
        plan->wide_offsets =
            (size_t *)malloc(fn->value_count * sizeof(*plan->wide_offsets));
        if (plan->wide_offsets == NULL) return false;
    }
    for (i = 0U; i < fn->value_count; ++i) {
        plan->wide_offsets[i] = SIZE_MAX;
        if (!minic_type_is_long_double(fn->values[i].type) &&
            !minic_type_is_int128_integer(fn->values[i].type)) continue;
        if (pos > MINIC_LARGE_FRAME_MAX - 16U) return false;
        plan->wide_offsets[i] = pos;
        pos += 16U;
    }
    plan->wide_base = wide_base;
    plan->wide_end = pos;
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
    *plan = (MinicLP64FramePlan){0};
}

/* AAPCS64 reserves a separate 224-byte varargs tag/GP/FP save region.
 * LoongArch LP64D uses 64 bytes of integer argument saves.
 * Preserve existing ABI-specific bounds, offset and stack alignment. */
bool minic_lp64_finalize_direct_frame(
    size_t object_end, bool has_calls, bool dynamic_frame, bool has_varargs,
    MinicLP64DirectABI abi, size_t *save_offset, size_t *va_base,
    size_t *frame_size) {
    size_t save, required, frame, va = 0U;
    if (save_offset == NULL || frame_size == NULL ||
        object_end > MINIC_LARGE_FRAME_MAX ||
        (abi != MINIC_LP64_ABI_AAPCS64 &&
         abi != MINIC_LP64_ABI_LOONGARCH64_LP64D))
        return false;
    save = (object_end + 7U) & ~(size_t)7U;
    if (abi == MINIC_LP64_ABI_AAPCS64 && has_varargs) {
        if (va_base == NULL || save > MINIC_LARGE_FRAME_MAX - 240U)
            return false;
        va = (save + 15U) & ~(size_t)15U;
        save = va + 224U;
    }
    required = (has_calls ? 8U : 0U) + (dynamic_frame ? 8U : 0U);
    if (abi == MINIC_LP64_ABI_LOONGARCH64_LP64D && has_varargs)
        required += 64U;
    if (save > MINIC_LARGE_FRAME_MAX - required) return false;
    frame = (save + required + 15U) & ~(size_t)15U;
    /* Expose the rejected size for the existing opt-in FRAME_CAP trace. */
    *frame_size = frame;
    if (frame > MINIC_LARGE_FRAME_MAX) return false;
    *save_offset = save;
    if (va_base != NULL) *va_base = va;
    return true;
}
