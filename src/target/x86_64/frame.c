#include "target/x86_64/frame.h"
#include "target/common/core_function_facts.h"
#include "target/common/core_frame_layout.h"
#include "target/common/core_value_slots.h"
#include "target/x86_64/abi.h"
#include "target/data_layout.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Exact x86 frame layout, moved out of assembly emission without changing
 * address arithmetic, 16+ aligned objects or SysV variadic save area. */
bool minic_x86_64_align(size_t value, size_t alignment, size_t *out) {
    return minic_core_frame_align_up(value, alignment, out);
}

/* Opt-in physical frame preflight stage trace. The frontend/core diagnosis
 * has already succeeded when this is reached; separating incoming SysV
 * placement, outgoing CALL placement, object storage and SSA slot layout
 * makes a real Linux ABI rejection actionable without relaxing any gate. */
static bool x86_frame_preflight_reject(
    const MinicCoreFunction *fn, const char *stage) {
    if (getenv("MINIC_M1_TRACE") != NULL && fn && stage)
        (void)fprintf(stderr,
            "MINIC_X86_FRAME_PREFLIGHT function=%s stage=%s parameters=%zu "
            "values=%zu objects=%zu\n",
            fn->name ? fn->name : "?", stage,
            fn->parameter_count, fn->value_count, fn->object_count);
    return false;
}








bool minic_x86_64_frame_init_with_storage(const MinicC0Program *program,
    const MinicTargetInfo *target, const MinicCoreFunction *fn,
    const MinicCoreFunctionFacts *facts,
    const MinicCoreObjectStoragePlan *storage, MinicX86Frame *frame) {
    size_t pos, i, value_bytes, maximum_alignment = 16U;
    if (program == NULL || target == NULL || fn == NULL || facts == NULL || frame == NULL ||
        fn->value_count > (size_t)INT32_MAX / 8U) return false;
    (void)memset(frame, 0, sizeof(*frame));
    frame->semantic_calls = facts->call_index;
    if (!minic_x86_64_plan_incoming_arguments(
            program, target, fn, &frame->incoming_arguments))
        return x86_frame_preflight_reject(fn, "incoming-sysv");
    if (facts->call_index != NULL &&
        !minic_x86_64_outgoing_call_plans_prepare(
            program, target, fn, facts->call_index,
            &frame->outgoing_call_plans))
        return x86_frame_preflight_reject(fn, "outgoing-sysv");
    if (!minic_core_object_storage_plan_acquire(
            program, target->data_layout, fn, storage,
            &frame->object_storage, &frame->borrows_object_storage))
        return x86_frame_preflight_reject(fn, "object-storage");
    frame->varargs_count = facts->variadic_address_count;
    frame->value_count = fn->value_count;
    frame->object_count = fn->object_count;
    /* Core owns per-value 8B/16B placement. SysV still decides that the
     * resulting end offsets are addressed below rbp and capped at INT32_MAX. */
    if (!minic_core_value_slots_build(
            fn, MINIC_CORE_VALUE_SLOT_LINEAR, 0U, (size_t)INT32_MAX,
            &frame->value_offsets, &value_bytes))
        return x86_frame_preflight_reject(fn, "value-slots");
    {
        size_t result_bytes;
        frame->has_sret = minic_x86_64_memory_result_record(
            program, target, fn->return_type, &result_bytes);
        if (frame->has_sret) {
            if (value_bytes > (size_t)INT32_MAX - 8U)
                return x86_frame_preflight_reject(fn, "sret-slot");
            value_bytes += 8U;
            frame->sret_save_offset = value_bytes;
        }
    }
    /* Freeze the entire incoming GP register bank before any Core
     * instruction can use RDX as the high limb of a temporary. Otherwise
     * an early __int128 parameter may destroy a later GP parameter before
     * its PARAMETER instruction executes. No overhead for functions without
     * TI parameters. 48 bytes also preserves 16-byte frame alignment. */
    for (i = 0U; i < fn->parameter_count; ++i)
        if (minic_type_is_int128_integer(fn->parameter_types[i])) {
            if (value_bytes > (size_t)INT32_MAX - 48U)
                return false;
            frame->saves_incoming_gp = true;
            frame->incoming_gp_save_offset = value_bytes + 8U;
            value_bytes += 48U;
            break;
        }
    /* Discover semantic va_start sites from Core. A VA cursor is not an RV64
     * sequential argument pointer under SysV x86-64. */
    if (frame->varargs_count != 0U) {
        size_t count = 0U, gp_used, fp_used, stack_used;
        if (frame->varargs_count > SIZE_MAX / sizeof(*frame->varargs_results) ||
            (frame->varargs_results = (MinicCoreValueId *)calloc(
                 frame->varargs_count, sizeof(*frame->varargs_results))) == NULL)
            return false;
        if (facts->variadic_address_results != NULL) {
            /* Borrow once-checked Common semantic result IDs in production. */
            for (i = 0U; i < frame->varargs_count; ++i) {
                const MinicCoreValueId result = facts->variadic_address_results[i];
                if (result >= fn->value_count) return false;
                frame->varargs_results[count++] = result;
            }
        } else {
            /* Standalone frame clients have no visitor-owned site index. */
            for (i = 0U; i < fn->instruction_count; ++i) {
                const MinicCoreInstruction *ins = &fn->instructions[i];
                if (ins->kind != MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS)
                    continue;
                if (count >= frame->varargs_count ||
                    ins->result >= fn->value_count) return false;
                frame->varargs_results[count++] = ins->result;
            }
        }
        if (count != frame->varargs_count) return false;
        /* The single SysV planner has already classified *all* named
         * parameters, including aggregates and empty GNU records. The
         * cursor below is therefore the ABI ground truth for va_start;
         * separately rejecting by-value records here would also reject
         * valid va_copy sites in non-variadic helper functions. */
        gp_used = frame->incoming_arguments.end.gp_used;
        fp_used = frame->incoming_arguments.end.fp_used;
        stack_used = frame->incoming_arguments.end.stack_slots;
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
        const size_t alignment = frame->object_storage.objects[i].alignment;
        /* SysV frame offsets are constrained to signed 32-bit displacements. */
        if (alignment > (size_t)INT32_MAX) return false;
        if (alignment > maximum_alignment) maximum_alignment = alignment;
    }
    frame->object_base_alignment = maximum_alignment;
    frame->objects_from_aligned_rsp = maximum_alignment > 16U;
    pos = frame->objects_from_aligned_rsp ? 0U :
          frame->varargs_count != 0U
              ? frame->varargs_tag_base_offset + frame->varargs_count * 24U
              : value_bytes;
    for (i = 0U; i < fn->object_count; ++i) {
        const size_t size = frame->object_storage.objects[i].extent;
        const size_t alignment = frame->object_storage.objects[i].alignment;
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
    minic_x86_64_argument_plan_destroy(&frame->incoming_arguments);
    minic_x86_64_outgoing_call_plans_destroy(&frame->outgoing_call_plans);
    minic_core_object_storage_plan_release(
        &frame->object_storage, frame->borrows_object_storage);
    free(frame->varargs_results);
    (void)memset(frame, 0, sizeof(*frame));
}

