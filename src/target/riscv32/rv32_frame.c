#include "target/riscv32/rv32_internal.h"
#include "target/riscv/machine_target_state.h"
#include "target/data_layout.h"
#include "target/common/core_function_facts.h"
#include "target/common/core_spill_layout.h"
#include "target/common/core_object_reuse.h"
#include "target/common/core_object_storage.h"
#include "target/common/core_frame_layout.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* ILP32 finalized frame planner. RV64's LP64-specific
 * dynamic-frame and 8-byte SSA slot assumptions are not imported. */
/* A failed real-world frame should identify the ownership boundary
 * in the same grouped acceptance run, rather than spend a CI cycle per
 * source file. This prints only on actual rejection when explicitly enabled. */
static bool rv32_frame_reject(const char *stage, size_t index,
                             const MinicCoreFunction *function,
                             MinicCoreTypeId type_id) {
    if (getenv("MINIC_RV32_FRAME_TRACE") != NULL) {
        const MinicCoreType *type = function != NULL &&
            function->type_context != NULL
            ? minic_core_type_get(function->type_context,
                  minic_core_type_unqualified(function->type_context, type_id))
            : NULL;
        (void)fprintf(stderr,
            "RV32_FRAME_REJECT stage=%s index=%zu core_type=%u kind=%d size=%zu bits=%u\n",
            stage, index, (unsigned)type_id,
            type != NULL ? (int)type->kind : -1,
            type != NULL && type->has_layout ? type->size : 0U,
            type != NULL && type->kind == MINIC_CORE_TYPE_INTEGER
                ? type->value.integer.bit_width : 0U);
    }
    return false;
}

static bool rv32_align(size_t value, size_t by, size_t *out) {
    return by != 0U && (by & (by - 1U)) == 0U &&
           minic_core_frame_align_up(value, by, out);
}

/* A wide ILP32 operand remains ONE semantic Core SSA value. */
bool rv32_frame_init_with_storage(Rv32Frame *frame,
    const MinicCoreFunction *f, const MinicTargetInfo *t,
    bool is_variadic, const MinicCoreFunctionFacts *facts,
    const MinicCoreObjectStoragePlan *storage) {
    size_t next, i, max_stack_args, value_base, peak_bytes = 0U;
    size_t maximum_object_alignment = 16U;
    if (frame == NULL || f == NULL || t == NULL || facts == NULL ||
        f->value_count > 100000U || f->object_count > 100000U)
        return false;
    if (!minic_riscv_target_isa_load(t, &frame->isa) ||
        frame->isa.xlen != 32U)
        return false;
    /* The ABI plan is built once; frame and call emission consume exactly
     * its precomputed stack bound and individual argument placements. */
    frame->has_dynamic_stack = facts->uses_dynamic_stack_alloc;
    if (!rv32_function_abi_plan_build(f, facts->call_index, &frame->abi))
        return rv32_frame_reject("abi_plan", 0U, f,
                                 MINIC_CORE_TYPE_INVALID);
    max_stack_args = frame->abi.max_outgoing_stack_words;
    frame->is_variadic = is_variadic;
    if (frame->abi.named_gpr_words > 8U ||
        frame->abi.named_stack_words > (size_t)INT32_MAX / 4U)
        return false;
    frame->varargs_size = is_variadic ?
        (8U - frame->abi.named_gpr_words) * 4U : 0U;
    if (max_stack_args > (size_t)INT32_MAX / 4U ||
        !rv32_align(max_stack_args * 4U, 16U, &frame->outgoing_size) ||
        f->value_count > (SIZE_MAX - frame->outgoing_size - 4U) / 8U)
        return false;
    frame->object_offsets = NULL;
    frame->value_offsets = NULL;
    frame->object_storage = (MinicCoreObjectStoragePlan){0};
    frame->borrows_object_storage = false;
    if (!minic_core_object_storage_plan_acquire(
            f, storage,
            &frame->object_storage, &frame->borrows_object_storage))
        return rv32_frame_reject("core_object_storage", 0U, f,
                                 MINIC_CORE_TYPE_INVALID);
    frame->object_count = f->object_count;
    frame->value_count = f->value_count;
    if (f->value_count != 0U) {
        frame->value_offsets = malloc(f->value_count * sizeof(size_t));
        if (frame->value_offsets == NULL) return false;
        for (i = 0U; i < f->value_count; ++i)
            frame->value_offsets[i] = SIZE_MAX;
    }
    if (f->object_count != 0U) {
        frame->object_offsets = calloc(f->object_count, sizeof(size_t));
        if (frame->object_offsets == NULL) return false;
    }
    /* Slot zero saves ra even in a leaf; a stable ABI-oriented frame makes
     * lowering of calls and arithmetic runtime helpers deterministic. */
    if (frame->outgoing_size > SIZE_MAX - (frame->abi.indirect_return ? 8U : 4U))
        return rv32_frame_reject("sret_frame_overflow", 0U, f,
                                 minic_core_function_return_type(f));
    frame->sret_offset = frame->outgoing_size + 4U;
    frame->saved_base_offset = frame->outgoing_size +
                               (frame->abi.indirect_return ? 8U : 4U);
    if (frame->has_dynamic_stack && frame->saved_base_offset > SIZE_MAX - 4U)
        return rv32_frame_reject("saved_base", 0U, f,
                                 minic_core_function_return_type(f));
    next = frame->saved_base_offset + (frame->has_dynamic_stack ? 4U : 0U);
    /* The already merged RV64 P1 first-write/escape/lifetime proof is
     * target-independent and lives in common CoreObject analysis.
     * Admit its safe cross-block reuse here too: ILP32 object extents and
     * ABI frame offsets remain RV32-owned. Disqualify ambiguous uses inside
     * the shared analyzer; never introduce a separate RV32 proof. */
    {
        MinicCoreObjectReusePlan reuse = {0};
        size_t pool_slots = 0U, pool_base;
        if (!minic_core_object_reuse_plan_build(
                f, 8U, true, &reuse) ||
            !minic_core_object_reuse_plan_get(
                &reuse, MINIC_CORE_OBJECT_INVALID, NULL, NULL, &pool_slots)) {
            minic_core_object_reuse_plan_destroy(&reuse);
            return rv32_frame_reject("core_object_plan", 0U, f,
                                     MINIC_CORE_TYPE_INVALID);
        }
        for (i = 0U; i < f->object_count; ++i) {
            const MinicCoreObject *obj = &f->objects[i];
            const MinicCoreObjectStorage *object_slot =
                &frame->object_storage.objects[i];
            size_t size, align, slot, peak;
            bool reusable;
            if (!minic_core_object_reuse_plan_get(
                    &reuse, (MinicCoreObjectId)i, &reusable, &slot, &peak) ||
                peak != pool_slots) {
                minic_core_object_reuse_plan_destroy(&reuse);
                return rv32_frame_reject("core_object_layout", i, f,
                                         obj->core_type_id);
            }
            size = object_slot->extent;
            align = object_slot->alignment;
            if (align > maximum_object_alignment)
                maximum_object_alignment = align;
            if (reusable) continue;
            if (align < 4U) align = 4U;
            if (!minic_core_frame_reserve(
                    &next, align, size != 0U ? size : 1U, SIZE_MAX,
                    &frame->object_offsets[i])) {
                minic_core_object_reuse_plan_destroy(&reuse);
                return rv32_frame_reject("object_offset", i, f,
                                         obj->core_type_id);
            }
        }
        if (!rv32_align(next, 8U, &pool_base) ||
            pool_slots > (SIZE_MAX - pool_base) / 8U) {
            minic_core_object_reuse_plan_destroy(&reuse);
            return rv32_frame_reject("object_pool", 0U, f,
                                     MINIC_CORE_TYPE_INVALID);
        }
        next = pool_base + pool_slots * 8U;
        for (i = 0U; i < f->object_count; ++i) {
            size_t slot, peak;
            bool reusable;
            if (!minic_core_object_reuse_plan_get(
                    &reuse, (MinicCoreObjectId)i, &reusable, &slot, &peak) ||
                peak != pool_slots || (reusable && slot >= pool_slots)) {
                minic_core_object_reuse_plan_destroy(&reuse);
                return rv32_frame_reject("object_pool_slot", i, f,
                                         f->objects[i].core_type_id);
            }
            if (reusable) frame->object_offsets[i] = pool_base + 8U * slot;
        }
        minic_core_object_reuse_plan_destroy(&reuse);
    }
    /* RV64-proven Core SSA last-use reuse is still block-local; ILP32
     * values use 4-byte physical units. CoreObject cross-block eligibility
     * is handled separately by the shared first-write safety proof above. */
    if (!rv32_align(next, 8U, &value_base))
        return rv32_frame_reject("spill_base", next, f,
                                 MINIC_CORE_TYPE_INVALID);
    {
        size_t failed_block = SIZE_MAX;
        if (!minic_core_function_reused_spill_layout(
                f, 4U, value_base,
                frame->value_offsets, &peak_bytes, &failed_block))
            return rv32_frame_reject("spill_layout", failed_block, f,
                                     MINIC_CORE_TYPE_INVALID);
    }
    if (value_base > SIZE_MAX - peak_bytes)
        return rv32_frame_reject("spill_total", 0U, f,
                                 MINIC_CORE_TYPE_INVALID);
    next = value_base + peak_bytes;
    /* RV64 already handled over-aligned Core locals: the incoming sp only
     * guarantees 16-byte ABI alignment, even when object offsets align to
     * 32 or 64. Align the real frame base and restore the ORIGINAL incoming
     * sp; otherwise an aligned(64) C object silently violates its contract.
     * The original incoming sp also anchors stack-passed named arguments. */
    frame->stack_alignment = maximum_object_alignment;
    frame->has_dynamic_stack_alignment = maximum_object_alignment > 16U;
    frame->entry_sp_offset = 0U;
    if (frame->has_dynamic_stack_alignment) {
        if (!rv32_align(next, 4U, &frame->entry_sp_offset) ||
            frame->entry_sp_offset > SIZE_MAX - 4U)
            return rv32_frame_reject("entry_sp_slot", 0U, f,
                                     MINIC_CORE_TYPE_INVALID);
        next = frame->entry_sp_offset + 4U;
    }
    if (next > SIZE_MAX - frame->varargs_size ||
        !rv32_align(next + frame->varargs_size, 16U, &frame->frame_size) ||
        frame->frame_size > (size_t)INT32_MAX ||
        frame->frame_size < frame->varargs_size)
        return rv32_frame_reject("frame_size", next, f,
                                 MINIC_CORE_TYPE_INVALID);
    frame->varargs_offset = frame->frame_size - frame->varargs_size;
    return frame->varargs_offset >= next;
}
void rv32_frame_destroy(Rv32Frame *f) {
    if (f != NULL) {
        free(f->object_offsets);
        free(f->value_offsets);
        minic_core_object_storage_plan_release(
            &f->object_storage, f->borrows_object_storage);
        f->object_offsets = NULL;
        f->value_offsets = NULL;
        rv32_function_abi_plan_destroy(&f->abi);
    }
}

bool rv32_machine_physical_frame(const Rv32Frame *frame,
    MinicRiscvPhysicalFrame *physical) {
    if (frame == NULL || physical == NULL ||
        frame->isa.xlen != 32U || frame->frame_size == 0U ||
        frame->abi.named_gpr_words > 8U ||
        frame->has_dynamic_stack_alignment !=
            (frame->stack_alignment > 16U) ||
        frame->outgoing_size > frame->frame_size ||
        (frame->value_count != 0U && frame->value_offsets == NULL) ||
        (frame->object_count != 0U && frame->object_offsets == NULL))
        return false;
    *physical = (MinicRiscvPhysicalFrame){
        .frame_size = frame->frame_size,
        .stack_alignment = frame->stack_alignment,
        .outgoing_size = frame->outgoing_size,
        .return_address_offset = frame->outgoing_size,
        .hidden_result_pointer_offset = frame->sret_offset,
        .saved_frame_base_offset = frame->saved_base_offset,
        .entry_sp_offset = frame->entry_sp_offset,
        .dynamic_call_area_size = 0U,
        .varargs_offset = frame->varargs_offset,
        .varargs_size = frame->varargs_size,
        .first_variadic_gpr = frame->abi.named_gpr_words,
        .named_stack_argument_slots = frame->abi.named_stack_words,
        .saves_return_address = true,
        .has_hidden_result_pointer = frame->abi.indirect_return,
        .has_dynamic_stack = frame->has_dynamic_stack,
        .has_dynamic_stack_alignment = frame->has_dynamic_stack_alignment,
        .variadic = frame->is_variadic
    };
    return true;
}
