#include "target/x86_64/frame.h"
#include "target/common/core_function_facts.h"
#include "target/common/core_frame_layout.h"
#include "target/common/core_object_reuse.h"
#include "target/common/core_spill_layout.h"
#include "target/x86_64/abi.h"
#include "target/data_layout.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Opt-in physical frame preflight stage trace. The frontend/core diagnosis
 * has already succeeded when this is reached; separating incoming ABI
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








/* Frame is caller-owned and must have no live resources on entry: init resets
 * it before creating plans/offsets. Failure may leave partial allocations;
 * the emitter destroys the frame on both successful and rejected paths. */
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
        return x86_frame_preflight_reject(fn, "incoming-abi");
    if (facts->call_index != NULL &&
        !minic_x86_64_outgoing_call_plans_prepare(
            program, target, fn, facts->call_index,
            &frame->outgoing_call_plans))
        return x86_frame_preflight_reject(fn, "outgoing-abi");
    if (!minic_core_object_storage_plan_acquire(
            program, target->data_layout, fn, storage,
            &frame->object_storage, &frame->borrows_object_storage))
        return x86_frame_preflight_reject(fn, "object-storage");
    frame->varargs_count = facts->variadic_address_count;
    frame->value_count = fn->value_count;
    frame->object_count = fn->object_count;
    /* Core's proven block-local last-use allocator supplies offsets from
     * the beginning of a positive-direction scratch area. SysV x86-64
     * still owns negative-rbp addressing: turn each start into the negative
     * end displacement that the native x86 machine emitters expect.
     * Keep the wide two-limb layout and all ABI frame placements unchanged. */
    if (fn->value_count != 0U) {
        if (fn->value_count > SIZE_MAX / sizeof(*frame->value_offsets) ||
            (frame->value_offsets = malloc(fn->value_count *
                                           sizeof(*frame->value_offsets))) == NULL)
            return x86_frame_preflight_reject(fn, "value-slots-alloc");
        for (i = 0U; i < fn->value_count; ++i)
            frame->value_offsets[i] = SIZE_MAX;
    }
    if (!minic_core_function_reused_spill_layout(
            program, fn, target->data_layout, 8U, 0U,
            frame->value_offsets, &value_bytes, NULL))
        return x86_frame_preflight_reject(fn, "value-slots-reuse");
    for (i = 0U; i < fn->value_count; ++i) {
        const MinicType type = fn->values[i].type;
        const size_t width = minic_type_is_int128_integer(type) ||
                             minic_type_is_long_double(type) ? 16U : 8U;
        if (frame->value_offsets[i] == SIZE_MAX ||
            frame->value_offsets[i] > (size_t)INT32_MAX - width)
            return x86_frame_preflight_reject(fn, "value-slots-reuse-offset");
        frame->value_offsets[i] += width;
    }
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
            const MinicX86AbiRegisters *abi = minic_x86_64_abi_registers(
                MINIC_CALLING_CONVENTION_DEFAULT);
            size_t bank_bytes;
            if (abi == NULL || abi->gp_argument_count > SIZE_MAX / 8U)
                return false;
            bank_bytes = abi->gp_argument_count * 8U;
            if (bank_bytes > (size_t)INT32_MAX ||
                value_bytes > (size_t)INT32_MAX - bank_bytes)
                return false;
            frame->saves_incoming_gp = true;
            frame->incoming_gp_save_offset = value_bytes + 8U;
            value_bytes += bank_bytes;
            break;
        }
    if (fn->calling_convention == MINIC_CALLING_CONVENTION_MS_X64) {
        const MinicX86AbiRegisters *abi = minic_x86_64_abi_registers(
            MINIC_CALLING_CONVENTION_MS_X64);
        size_t gp_bytes, xmm_bytes, nonvolatile_bytes;
        /* Win64 extends the SysV nonvolatile bank by RDI/RSI and XMM6..15.
         * Save all ten vector registers: even a body which only emits
         * scalar code can invoke arbitrary SysV callees (where those vector
         * registers are volatile). Reserve the two GP slots first, then
         * ten 16-byte vector slots, preserving Core value ownership. */
        if (abi == NULL || abi->extra_nonvolatile_gp == NULL ||
            abi->extra_nonvolatile_gp_count == 0U ||
            abi->extra_nonvolatile_gp_count > SIZE_MAX / 8U ||
            abi->extra_nonvolatile_xmm_count > SIZE_MAX / 16U ||
            frame->saves_incoming_gp || frame->has_sret ||
            frame->varargs_count != 0U)
            return x86_frame_preflight_reject(fn, "ms-x64-nonvolatile");
        gp_bytes = abi->extra_nonvolatile_gp_count * 8U;
        xmm_bytes = abi->extra_nonvolatile_xmm_count * 16U;
        if (gp_bytes > SIZE_MAX - xmm_bytes ||
            (nonvolatile_bytes = gp_bytes + xmm_bytes) > (size_t)INT32_MAX ||
            value_bytes > (size_t)INT32_MAX - nonvolatile_bytes)
            return x86_frame_preflight_reject(fn, "ms-x64-nonvolatile");
        frame->ms_nonvolatile_gp_save_offset = value_bytes + 8U;
        frame->ms_nonvolatile_xmm_save_offset = value_bytes + gp_bytes + 16U;
        value_bytes += nonvolatile_bytes;
    }
    /* Discover semantic va_start sites from Core. A VA cursor is not an RV64
     * sequential argument pointer under SysV x86-64. */
    if (frame->varargs_count != 0U) {
        const MinicX86AbiRegisters *abi = minic_x86_64_abi_registers(
            MINIC_CALLING_CONVENTION_DEFAULT);
        size_t count = 0U, gp_used, fp_used, stack_used;
        size_t save_gp_bytes, save_fp_bytes, save_bytes;
        if (abi == NULL || abi->gp_argument_count > SIZE_MAX / 8U ||
            abi->fp_argument_count > SIZE_MAX / 16U)
            return false;
        save_gp_bytes = abi->gp_argument_count * 8U;
        save_fp_bytes = abi->fp_argument_count * 16U;
        if (save_gp_bytes > SIZE_MAX - save_fp_bytes) return false;
        save_bytes = save_gp_bytes + save_fp_bytes;
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
            save_bytes > (size_t)INT32_MAX ||
            value_bytes > (size_t)INT32_MAX - save_bytes ||
            !minic_core_frame_align_up(value_bytes + save_bytes, 16U,
                                       &frame->varargs_regsave_offset) ||
            frame->varargs_count > ((size_t)INT32_MAX -
                                    frame->varargs_regsave_offset) / 24U)
            return false;
        frame->varargs_tag_base_offset = frame->varargs_regsave_offset;
        frame->varargs_fixed_stack_slots = stack_used;
        frame->varargs_gp_offset = (unsigned int)(gp_used * 8U);
        frame->varargs_fp_offset = (unsigned int)(save_gp_bytes + fp_used * 16U);
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
    if (!frame->objects_from_aligned_rsp) {
        MinicCoreObjectReusePlan reuse = {0};
        size_t pool_slots, pool_first_end = 0U;
        bool valid = minic_core_object_reuse_plan_build(
            program, fn, target->data_layout, 8U, true, &reuse) &&
            minic_core_object_reuse_plan_get(
                &reuse, MINIC_CORE_OBJECT_INVALID, NULL, NULL, &pool_slots);
        /* Core proves first-write, last-use and non-escape of every eligible
         * scalar object. A separate 8-byte slot pool lets unrolled kernel
         * crypto stay within the 16KiB kernel stack without weakening
         * address identity for other (potentially escaping) objects. */
        for (i = 0U; valid && i < fn->object_count; ++i) {
            const size_t size = frame->object_storage.objects[i].extent;
            const size_t alignment = frame->object_storage.objects[i].alignment;
            size_t slot, peak;
            bool reusable;
            valid = minic_core_object_reuse_plan_get(
                &reuse, (MinicCoreObjectId)i, &reusable, &slot, &peak) &&
                peak == pool_slots;
            if (!valid || reusable) continue;
            /* Negative offsets from aligned %rbp denote the object's low
             * address after subtracting its END displacement. */
            valid = pos <= SIZE_MAX - size &&
                minic_core_frame_align_up(pos + size, alignment, &pos) &&
                pos <= (size_t)INT32_MAX;
            if (valid) frame->object_offsets[i] = pos;
        }
        if (valid) {
            valid = minic_core_frame_align_up(pos, 8U, &pos) &&
                pos <= (size_t)INT32_MAX &&
                pool_slots <= ((size_t)INT32_MAX - pos) / 8U;
            if (valid && pool_slots != 0U) {
                pool_first_end = pos + 8U;
                pos += pool_slots * 8U;
            }
        }
        for (i = 0U; valid && i < fn->object_count; ++i) {
            size_t slot, peak;
            bool reusable;
            valid = minic_core_object_reuse_plan_get(
                &reuse, (MinicCoreObjectId)i, &reusable, &slot, &peak) &&
                peak == pool_slots && (!reusable || slot < pool_slots);
            if (valid && reusable)
                frame->object_offsets[i] = pool_first_end + slot * 8U;
        }
        minic_core_object_reuse_plan_destroy(&reuse);
        if (!valid) return false;
    } else {
        for (i = 0U; i < fn->object_count; ++i) {
            const size_t size = frame->object_storage.objects[i].extent;
            const size_t alignment = frame->object_storage.objects[i].alignment;
            /* Positive offsets from dynamically aligned %rsp denote START. */
            if (!minic_core_frame_align_up(pos, alignment, &pos) ||
                pos > SIZE_MAX - size) return false;
            frame->object_offsets[i] = pos;
            pos += size;
            if (pos > (size_t)INT32_MAX) return false;
        }
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
    if (!minic_core_frame_align_up(pos, 16U, &frame->frame_size) ||
        frame->frame_size > (size_t)INT32_MAX) return false;
    if (getenv("MINIC_X86_FRAME_TRACE") != NULL && fn->name != NULL &&
        strstr(fn->name, "blake2s_compress_generic") != NULL) {
        (void)fprintf(stderr,
            "MINIC_X86_FRAME function=%s scratch_bytes=%zu "
            "allocated_object_bytes=%zu objects=%zu values=%zu final_frame=%zu\n",
            fn->name, value_bytes,
            pos >= value_bytes ? pos - value_bytes : 0U,
            fn->object_count, fn->value_count, frame->frame_size);
        if (getenv("MINIC_X86_FRAME_DUMP") != NULL)
            (void)minic_core_function_dump(stderr, fn);
    }
    return true;
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

