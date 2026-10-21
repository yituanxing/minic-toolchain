#include "target/x86_64/frame.h"
#include "target/x86_64/abi.h"
#include "target/data_layout.h"
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Original SysV va_list, aligned object and Core SSA spill layout.
 * The target-owned machine emitter still reads exactly this frame plan. */
bool minic_x86_64_align(size_t value, size_t alignment, size_t *out) {
    size_t padding;
    if (out == NULL || alignment == 0U) return false;
    padding = (alignment - value % alignment) % alignment;
    if (value > SIZE_MAX - padding) return false;
    *out = value + padding;
    return true;
}

bool minic_x86_64_frame_init(const MinicC0Program *program, const MinicTargetInfo *target,
                           const MinicCoreFunction *fn, MinicX86Frame *frame) {
    size_t pos, i, value_bytes, maximum_alignment = 16U;
    if (program == NULL || target == NULL || fn == NULL || frame == NULL ||
        fn->value_count > (size_t)INT32_MAX / 8U) return false;
    (void)memset(frame, 0, sizeof(*frame));
    frame->value_count = fn->value_count;
    frame->object_count = fn->object_count;
    value_bytes = 0U;
    if (fn->value_count != 0U) {
        frame->value_offsets = (size_t *)calloc(fn->value_count,
                                                 sizeof(*frame->value_offsets));
        if (frame->value_offsets == NULL) return false;
    }
    for (i = 0U; i < fn->value_count; ++i) {
        size_t slot = (minic_type_is_int128_integer(fn->values[i].type) ||
                       minic_type_is_long_double(fn->values[i].type)) ? 16U : 8U;
        if (value_bytes > (size_t)INT32_MAX - slot) return false;
        value_bytes += slot;
        frame->value_offsets[i] = value_bytes;
    }
    /* Discover semantic va_start sites from Core. A VA cursor is not an RV64
     * sequential argument pointer under SysV x86-64. */
    for (i = 0U; i < fn->instruction_count; ++i) {
        if (fn->instructions[i].kind == MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS)
            ++frame->varargs_count;
    }
    if (frame->varargs_count != 0U) {
        size_t count = 0U, gp_used = 0U, fp_used = 0U, stack_used = 0U;
        size_t width;
        bool ignored_signed;
        if (frame->varargs_count > SIZE_MAX / sizeof(*frame->varargs_results) ||
            (frame->varargs_results = (MinicCoreValueId *)calloc(
                 frame->varargs_count, sizeof(*frame->varargs_results))) == NULL)
            return false;
        for (i = 0U; i < fn->instruction_count; ++i) {
            const MinicCoreInstruction *ins = &fn->instructions[i];
            if (ins->kind == MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS) {
                if (ins->result >= fn->value_count) return false;
                frame->varargs_results[count++] = ins->result;
            }
        }
        for (i = 0U; i < fn->parameter_count; ++i) {
            MinicType p = fn->parameter_types[i];
            if (minic_type_is_double(p) || minic_type_is_float(p)) {
                if (fp_used < 8U) ++fp_used;
                else ++stack_used;
            } else {
                if (!minic_x86_64_scalar_width(program, target, p, &width, &ignored_signed))
                    return false; /* Aggregate SysV classification not yet implemented. */
                if (gp_used < 6U) ++gp_used;
                else ++stack_used;
            }
        }
        if (stack_used > ((size_t)INT32_MAX - 16U) / 8U ||
            value_bytes > (size_t)INT32_MAX - 176U ||
            !minic_x86_64_align(value_bytes + 176U, 16U, &frame->varargs_regsave_offset) ||
            frame->varargs_count > ((size_t)INT32_MAX -
                                    frame->varargs_regsave_offset) / 24U)
            return false;
        frame->varargs_tag_base_offset = frame->varargs_regsave_offset;
        frame->varargs_fixed_stack_slots = stack_used;
        frame->varargs_gp_offset = (unsigned int)(gp_used * 8U);
        frame->varargs_fp_offset = (unsigned int)(48U + fp_used * 16U);
    }
    if (fn->object_count != 0U) {
        if (fn->object_count > SIZE_MAX / sizeof(*frame->object_offsets)) return false;
        frame->object_offsets = (size_t *)calloc(fn->object_count, sizeof(*frame->object_offsets));
        if (frame->object_offsets == NULL) return false;
    }
    /* %rbp is only 16-byte aligned by the SysV function entry invariant.
     * Above 16, a separate %rsp-relative, dynamically aligned object base
     * is needed.  An offset aligned relative to %rbp is NOT enough. */
    for (i = 0U; i < fn->object_count; ++i) {
        const MinicCoreObject *obj = &fn->objects[i];
        size_t size, alignment;
        if (!minic_data_layout_type(target->data_layout, program, obj->type,
                                    &size, &alignment)) return false;
        if (obj->explicit_alignment > alignment) alignment = obj->explicit_alignment;
        if (alignment == 0U || (alignment & (alignment - 1U)) != 0U ||
            alignment > (size_t)INT32_MAX) return false;
        if (alignment > maximum_alignment) maximum_alignment = alignment;
    }
    frame->object_base_alignment = maximum_alignment;
    frame->objects_from_aligned_rsp = maximum_alignment > 16U;
    pos = frame->objects_from_aligned_rsp ? 0U :
          frame->varargs_count != 0U
              ? frame->varargs_tag_base_offset + frame->varargs_count * 24U
              : value_bytes;
    for (i = 0U; i < fn->object_count; ++i) {
        const MinicCoreObject *obj = &fn->objects[i];
        size_t size, alignment;
        if (!minic_data_layout_type(target->data_layout, program, obj->type,
                                    &size, &alignment) ||
            obj->element_count == 0U || size > SIZE_MAX / obj->element_count)
            return false;
        size *= obj->element_count;
        if (obj->explicit_alignment > alignment) alignment = obj->explicit_alignment;
        if (frame->objects_from_aligned_rsp) {
            /* Positive offsets from the aligned %rsp denote object START. */
            if (!minic_x86_64_align(pos, alignment, &pos) || pos > SIZE_MAX - size)
                return false;
            frame->object_offsets[i] = pos;
            pos += size;
        } else {
            /* Negative offsets from aligned %rbp denote object START.
             * Align the END of the reserved extent, not its low address. */
            if (pos > SIZE_MAX - size ||
                !minic_x86_64_align(pos + size, alignment, &pos))
                return false;
            frame->object_offsets[i] = pos;
        }
        if (pos > (size_t)INT32_MAX) return false;
    }
    if (frame->objects_from_aligned_rsp) {
        /* This slack leaves dynamically aligned objects below all 8-byte
         * Core slots even when %rsp rounds down by alignment - 1 bytes. */
        size_t fixed_bytes = frame->varargs_count != 0U
            ? frame->varargs_tag_base_offset + frame->varargs_count * 24U
            : value_bytes;
        if (pos > SIZE_MAX - fixed_bytes ||
            pos + fixed_bytes > SIZE_MAX - maximum_alignment)
            return false;
        pos += fixed_bytes + maximum_alignment;
    }
    return minic_x86_64_align(pos, 16U, &frame->frame_size) &&
           frame->frame_size <= (size_t)INT32_MAX;
}

void minic_x86_64_frame_destroy(MinicX86Frame *frame) {
    if (frame == NULL) return;
    free(frame->value_offsets);
    free(frame->object_offsets);
    free(frame->varargs_results);
    (void)memset(frame, 0, sizeof(*frame));
}
