#include "target/riscv32/rv32_internal.h"
#include "target/riscv/register_identity.h"
#include "target/data_layout.h"
#include "target/common/core_function_facts.h"
#include "target/common/core_spill_layout.h"
#include "target/common/core_object_reuse.h"
#include "target/common/core_object_storage.h"
#include "target/common/core_frame_layout.h"
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ILP32 frame planner and stack word transport. RV64's LP64-specific
 * dynamic-frame and 8-byte SSA slot assumptions are not imported. */
/* A failed real-world frame should identify the ownership boundary
 * in the same grouped acceptance run, rather than spend a CI cycle per
 * source file. This prints only on actual rejection when explicitly enabled. */
static bool rv32_frame_reject(const char *stage, size_t index, MinicType type) {
    if (getenv("MINIC_RV32_FRAME_TRACE") != NULL)
        (void)fprintf(stderr,
            "RV32_FRAME_REJECT stage=%s index=%zu kind=%d depth=%u rank=%d\n",
            stage, index, (int)type.base_kind,
            (unsigned)type.pointer_depth, (int)type.integer_rank);
    return false;
}

static bool rv32_align(size_t value, size_t by, size_t *out) {
    return by != 0U && (by & (by - 1U)) == 0U &&
           minic_core_frame_align_up(value, by, out);
}

/* A wide ILP32 operand remains ONE semantic Core SSA value. */
bool rv32_frame_init_with_storage(Rv32Frame *frame, const MinicC0Program *program,
    const MinicCoreFunction *f, const MinicTargetInfo *t,
    bool is_variadic, const MinicCoreFunctionFacts *facts,
    const MinicCoreObjectStoragePlan *storage) {
    size_t next, i, max_stack_args, value_base, peak_bytes = 0U;
    size_t maximum_object_alignment = 16U;
    if (frame == NULL || program == NULL || f == NULL || t == NULL || facts == NULL ||
        f->value_count > 100000U || f->object_count > 100000U)
        return false;
    frame->isa = &t->riscv_isa;
    /* The ABI plan is built once; frame and call emission consume exactly
     * its precomputed stack bound and individual argument placements. */
    frame->has_dynamic_stack = facts->uses_dynamic_stack_alloc;
    if (!rv32_function_abi_plan_build(program, t, f, facts->call_index, &frame->abi))
        return rv32_frame_reject("abi_plan", 0U, minic_type_void());
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
            program, t->data_layout, f, storage,
            &frame->object_storage, &frame->borrows_object_storage))
        return rv32_frame_reject("core_object_storage", 0U, minic_type_void());
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
        return rv32_frame_reject("sret_frame_overflow", 0U, f->return_type);
    frame->sret_offset = frame->outgoing_size + 4U;
    frame->saved_base_offset = frame->outgoing_size +
                               (frame->abi.indirect_return ? 8U : 4U);
    if (frame->has_dynamic_stack && frame->saved_base_offset > SIZE_MAX - 4U)
        return rv32_frame_reject("saved_base", 0U, f->return_type);
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
                program, f, t->data_layout, 8U, true, &reuse) ||
            !minic_core_object_reuse_plan_get(
                &reuse, MINIC_CORE_OBJECT_INVALID, NULL, NULL, &pool_slots)) {
            minic_core_object_reuse_plan_destroy(&reuse);
            return rv32_frame_reject("core_object_plan", 0U, minic_type_void());
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
                return rv32_frame_reject("core_object_layout", i, obj->type);
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
                return rv32_frame_reject("object_offset", i, obj->type);
            }
        }
        if (!rv32_align(next, 8U, &pool_base) ||
            pool_slots > (SIZE_MAX - pool_base) / 8U) {
            minic_core_object_reuse_plan_destroy(&reuse);
            return rv32_frame_reject("object_pool", 0U, minic_type_void());
        }
        next = pool_base + pool_slots * 8U;
        for (i = 0U; i < f->object_count; ++i) {
            size_t slot, peak;
            bool reusable;
            if (!minic_core_object_reuse_plan_get(
                    &reuse, (MinicCoreObjectId)i, &reusable, &slot, &peak) ||
                peak != pool_slots || (reusable && slot >= pool_slots)) {
                minic_core_object_reuse_plan_destroy(&reuse);
                return rv32_frame_reject("object_pool_slot", i, f->objects[i].type);
            }
            if (reusable) frame->object_offsets[i] = pool_base + 8U * slot;
        }
        minic_core_object_reuse_plan_destroy(&reuse);
    }
    /* RV64-proven Core SSA last-use reuse is still block-local; ILP32
     * values use 4-byte physical units. CoreObject cross-block eligibility
     * is handled separately by the shared first-write safety proof above. */
    if (!rv32_align(next, 8U, &value_base))
        return rv32_frame_reject("spill_base", next, minic_type_void());
    {
        size_t failed_block = SIZE_MAX;
        if (!minic_core_function_reused_spill_layout(
                program, f, t->data_layout, 4U, value_base,
                frame->value_offsets, &peak_bytes, &failed_block))
            return rv32_frame_reject("spill_layout", failed_block,
                                     minic_type_void());
    }
    if (value_base > SIZE_MAX - peak_bytes)
        return rv32_frame_reject("spill_total", 0U, minic_type_void());
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
            return rv32_frame_reject("entry_sp_slot", 0U, minic_type_void());
        next = frame->entry_sp_offset + 4U;
    }
    if (next > SIZE_MAX - frame->varargs_size ||
        !rv32_align(next + frame->varargs_size, 16U, &frame->frame_size) ||
        frame->frame_size > (size_t)INT32_MAX ||
        frame->frame_size < frame->varargs_size)
        return rv32_frame_reject("frame_size", next, minic_type_void());
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
/* Outgoing call words use moving sp. All fixed Core SSA/objects use s0
 * when dynamic alloca changes sp. t6 owns large-displacement addressing. */
static bool rv32_stack_at(FILE *out, const char *base, const char *op,
                          const char *reg, size_t off) {
    if (out == NULL || base == NULL || op == NULL || reg == NULL ||
        off > (size_t)INT32_MAX)
        return false;
    /* t6 is the large-displacement address scratch. Storing t6 through
     * t6 would overwrite the original payload before sw; refuse rather
     * than silently generate corrupt machine code. Loads into t6 remain
     * valid, since its old value is not an input. */
    if (off > 2047U && minic_riscv_gpr_name_equal(reg, "t6") &&
        (strcmp(op, "sw") == 0 || strcmp(op, "sb") == 0 ||
         strcmp(op, "sh") == 0))
        return false;
    if (off <= 2047U)
        return fprintf(out, "  %s %s, %zu(%s)\n", op, reg, off, base) >= 0;
    return fprintf(out, "  li t6, %zu\n  add t6, %s, t6\n"
                        "  %s %s, 0(t6)\n", off, base, op, reg) >= 0;
}
bool rv32_stack(FILE *out, const char *op, const char *reg, size_t off) {
    return rv32_stack_at(out, "sp", op, reg, off);
}
bool rv32_fixed_stack(FILE *out, const Rv32Frame *fr,
                      const char *op, const char *reg, size_t off) {
    return fr != NULL && rv32_stack_at(
        out, fr->has_dynamic_stack ? "s0" : "sp", op, reg, off);
}
/* A valid Core ID is not itself proof of an allocated machine slot.
 * Uninitialized entries use SIZE_MAX, and must never be printed as an
 * immediate or wrapped by the two-word access below. */
bool rv32_slot(FILE *out, const Rv32Frame *f,
                       MinicCoreValueId id, const char *reg, bool load) {
    size_t off;
    if (out == NULL || f == NULL || reg == NULL ||
        f->value_offsets == NULL || id == MINIC_CORE_VALUE_INVALID ||
        (size_t)id >= f->value_count)
        return false;
    off = f->value_offsets[id];
    return off != SIZE_MAX && off <= (size_t)INT32_MAX &&
           off <= f->frame_size && f->frame_size - off >= 4U &&
           rv32_fixed_stack(out, f, load ? "lw" : "sw", reg, off);
}
/* Low-word first: RISC-V ELF32 and the little-endian ILP32 psABI. */
bool rv32_pair_slot(FILE *out, const Rv32Frame *f,
                           MinicCoreValueId id,
                           const char *low, const char *high, bool load) {
    size_t off;
    if (out == NULL || f == NULL || low == NULL || high == NULL ||
        f->value_offsets == NULL || id == MINIC_CORE_VALUE_INVALID ||
        (size_t)id >= f->value_count)
        return false;
    off = f->value_offsets[id];
    if (off == SIZE_MAX || off > (size_t)INT32_MAX - 4U ||
        off > f->frame_size || f->frame_size - off < 8U)
        return false;
    return rv32_fixed_stack(out, f, load ? "lw" : "sw", low, off) &&
           rv32_fixed_stack(out, f, load ? "lw" : "sw", high, off + 4U);
}
bool rv32_address(FILE *out, const Rv32Frame *fr,
                  const char *reg, size_t off) {
    const char *base;
    if (fr == NULL || reg == NULL) return false;
    base = fr->has_dynamic_stack ? "s0" : "sp";
    /* Materializing a large displacement destroys the destination first;
     * spelling the live base as x2/x8 does not make it a fresh register. */
    if (minic_riscv_gpr_name_equal(reg, base)) return false;
    return off <= 2047U ?
        fprintf(out, "  addi %s, %s, %zu\n", reg, base, off) >= 0 :
        fprintf(out, "  li %s, %zu\n  add %s, %s, %s\n",
                reg, off, reg, base, reg) >= 0;
}
bool rv32_incoming_word(FILE *out, const Rv32Frame *fr,
                               size_t word, const char *reg) {
    if (out == NULL || fr == NULL || reg == NULL) return false;
    if (word < 8U)
        return fprintf(out, "  mv %s, a%zu\n", reg, word) >= 0;
    /* Dynamic frame realignment means caller arguments are relative to
     * the saved ENTRY sp, not the frame_size offset from aligned sp.
     * Mirror RV64's entry_sp_offset transport for this ABI boundary. */
    if (fr->has_dynamic_stack_alignment) {
        size_t off;
        if (word - 8U > SIZE_MAX / 4U ||
            minic_riscv_gpr_name_equal(reg, "t5") ||
            minic_riscv_gpr_name_equal(reg, "t6") ||
            !rv32_fixed_stack(out, fr, "lw", "t6", fr->entry_sp_offset))
            return false;
        off = (word - 8U) * 4U;
        if (off <= 2047U)
            return fprintf(out, "  lw %s, %zu(t6)\n", reg, off) >= 0;
        return fprintf(out,
                       "  li t5, %zu\n  add t5, t6, t5\n  lw %s, 0(t5)\n",
                       off, reg) >= 0;
    }
    return word - 8U <= (SIZE_MAX - fr->frame_size) / 4U &&
           rv32_fixed_stack(out, fr, "lw", reg,
                             fr->frame_size + (word - 8U) * 4U);
}
bool rv32_outgoing_word(FILE *out, const Rv32Frame *fr,
                               size_t word, const char *reg) {
    if (out == NULL || fr == NULL || reg == NULL) return false;
    if (word < 8U)
        return fprintf(out, "  mv a%zu, %s\n", word, reg) >= 0;
    return word - 8U < fr->outgoing_size / 4U &&
           rv32_stack(out, "sw", reg, (word - 8U) * 4U);
}
