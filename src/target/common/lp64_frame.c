#include "target/common/lp64_frame.h"

/* Replaceable bounded frame algorithm (direct emitter mode). */
#define MINIC_LARGE_FRAME_MAX 1048576U

bool minic_lp64_frame_objects(const MinicC0Program *program,
                          const MinicTargetInfo *target,
                          const MinicCoreFunction *fn,
                          size_t sought_id, size_t *sought_offset,
                          size_t *object_end) {
    size_t pos, i;
    if (program == NULL || target == NULL || fn == NULL ||
        sought_id > fn->object_count ||
        fn->value_count > 32768U || fn->object_count > 8192U)
        return false;
    pos = fn->value_count * 8U;
    for (i = 0U; i < fn->object_count; ++i) {
        const MinicCoreObject *obj = &fn->objects[i];
        size_t unit, align, extent;
        if (!minic_data_layout_type(target->data_layout, program,
                                    obj->type, &unit, &align) ||
            obj->element_count == 0U || unit > MINIC_LARGE_FRAME_MAX / obj->element_count)
            return false;
        extent = unit * obj->element_count;
        if (obj->explicit_alignment > align)
            align = obj->explicit_alignment;
        if (align == 0U || align > 16U || (align & (align - 1U)) != 0U)
            return false;
        if (pos > MINIC_LARGE_FRAME_MAX - (align - 1U)) return false;
        pos = (pos + align - 1U) & ~(align - 1U);
        if (i == sought_id && sought_offset != NULL)
            *sought_offset = pos;
        if (extent > MINIC_LARGE_FRAME_MAX - pos) return false;
        pos += extent;
    }
    if (sought_id == fn->object_count && sought_offset != NULL)
        *sought_offset = pos;
    if (object_end != NULL) *object_end = pos;
    return true;
}

bool minic_lp64_binary128_slot(const MinicCoreFunction *fn, size_t base,
                           MinicCoreValueId id, size_t *out) {
    size_t i, pos = base;
    if (fn == NULL || out == NULL || id > fn->value_count ||
        base > MINIC_LARGE_FRAME_MAX) return false;
    for (i = 0U; i < fn->value_count; ++i) {
        if (!minic_type_is_long_double(fn->values[i].type) &&
            !minic_type_is_int128_integer(fn->values[i].type)) continue;
        if (pos > MINIC_LARGE_FRAME_MAX - 16U) return false;
        if (id == i) { *out = pos; return true; }
        pos += 16U;
    }
    if (id == fn->value_count) { *out = pos; return true; }
    return false;
}

/* AAPCS64 reserves a separate 224-byte varargs tag/GP/FP save region.
 * LoongArch LP64D uses 64 bytes of integer argument saves.
 * Preserve existing ABI-specific bounds, offset and stack alignment. */
bool minic_lp64_finalize_direct_frame(
    size_t object_end, bool has_calls, bool dynamic_frame, bool has_varargs,
    MinicLP64DirectFrameABI abi, size_t *save_offset, size_t *va_base,
    size_t *frame_size) {
    size_t save, required, frame, va = 0U;
    if (save_offset == NULL || frame_size == NULL ||
        object_end > MINIC_LARGE_FRAME_MAX ||
        (abi != MINIC_LP64_FRAME_AAPCS64 &&
         abi != MINIC_LP64_FRAME_LOONGARCH64_LP64D))
        return false;
    save = (object_end + 7U) & ~(size_t)7U;
    if (abi == MINIC_LP64_FRAME_AAPCS64 && has_varargs) {
        if (va_base == NULL || save > MINIC_LARGE_FRAME_MAX - 240U)
            return false;
        va = (save + 15U) & ~(size_t)15U;
        save = va + 224U;
    }
    required = (has_calls ? 8U : 0U) + (dynamic_frame ? 8U : 0U);
    if (abi == MINIC_LP64_FRAME_LOONGARCH64_LP64D && has_varargs)
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
