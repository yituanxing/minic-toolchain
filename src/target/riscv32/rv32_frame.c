#include "target/riscv32/rv32_internal.h"
#include "target/data_layout.h"
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

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
    if (by == 0U || (by & (by - 1U)) != 0U ||
        value > SIZE_MAX - (by - 1U)) return false;
    *out = (value + (by - 1U)) & ~(by - 1U);
    return true;
}

/* A wide ILP32 operand remains ONE semantic Core SSA value. */
bool rv32_frame_init(Rv32Frame *frame, const MinicC0Program *program,
                            const MinicCoreFunction *f, const MinicTargetInfo *t,
                            bool is_variadic) {
    size_t next, i, max_stack_args;
    if (frame == NULL || program == NULL || f == NULL || t == NULL ||
        f->value_count > 100000U || f->object_count > 100000U)
        return false;
    /* The ABI plan is built once; frame and call emission consume exactly
     * its precomputed stack bound and individual argument placements. */
    if (!rv32_function_abi_plan_build(program, t, f, &frame->abi))
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
    frame->object_count = f->object_count;
    frame->value_count = f->value_count;
    if (f->value_count != 0U) {
        frame->value_offsets = calloc(f->value_count, sizeof(size_t));
        if (frame->value_offsets == NULL) return false;
    }
    if (f->object_count != 0U) {
        frame->object_offsets = calloc(f->object_count, sizeof(size_t));
        if (frame->object_offsets == NULL) return false;
    }
    /* Slot zero saves ra even in a leaf; a stable ABI-oriented frame makes
     * lowering of calls and arithmetic runtime helpers deterministic. */
    next = frame->outgoing_size + 4U;
    for (i = 0U; i < f->value_count; ++i) {
        size_t size, alignment;
        unsigned words;
        if (!rv32_scalar_storage_words(f, f->values[i].type, &words) ||
            !minic_data_layout_type(t->data_layout, program,
                                    f->values[i].type, &size, &alignment) ||
            size == 0U || size > 8U ||
            !rv32_align(next, words == 2U ? 8U : 4U, &next) ||
            next > SIZE_MAX - words * 4U)
            return rv32_frame_reject("value_slot", i, f->values[i].type);
        frame->value_offsets[i] = next;
        next += words * 4U;
    }
    for (i = 0; i < f->object_count; ++i) {
        const MinicCoreObject *obj = &f->objects[i];
        size_t size, align;
        if (!minic_data_layout_type(t->data_layout, program,
                                    obj->type, &size, &align) ||
            (obj->element_count > 1U &&
             size > SIZE_MAX / obj->element_count))
            return rv32_frame_reject("object_layout", i, obj->type);
        if (obj->element_count > 1U) size *= obj->element_count;
        if (obj->explicit_alignment > align) align = obj->explicit_alignment;
        if (align < 4U) align = 4U;
        if (!rv32_align(next, align, &next) || size > SIZE_MAX - next)
            return rv32_frame_reject("object_offset", i, obj->type);
        frame->object_offsets[i] = next;
        next += size != 0U ? size : 1U;
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
        f->object_offsets = NULL;
        f->value_offsets = NULL;
        rv32_function_abi_plan_destroy(&f->abi);
    }
}
/* t6 is reserved as the large-displacement scratch register. */
bool rv32_stack(FILE *out, const char *op, const char *reg, size_t off) {
    if (off <= 2047U)
        return fprintf(out, "  %s %s, %zu(sp)\n", op, reg, off) >= 0;
    return fprintf(out, "  li t6, %zu\n  add t6, sp, t6\n"
                        "  %s %s, 0(t6)\n", off, op, reg) >= 0;
}
bool rv32_slot(FILE *out, const Rv32Frame *f,
                       MinicCoreValueId id, const char *reg, bool load) {
    return id != MINIC_CORE_VALUE_INVALID && (size_t)id < f->value_count &&
           rv32_stack(out, load ? "lw" : "sw", reg,
                      f->value_offsets[id]);
}
/* Low-word first: RISC-V ELF32 and the little-endian ILP32 psABI. */
bool rv32_pair_slot(FILE *out, const Rv32Frame *f,
                           MinicCoreValueId id,
                           const char *low, const char *high, bool load) {
    return id != MINIC_CORE_VALUE_INVALID && (size_t)id < f->value_count &&
           rv32_stack(out, load ? "lw" : "sw", low,
                      f->value_offsets[id]) &&
           rv32_stack(out, load ? "lw" : "sw", high,
                      f->value_offsets[id] + 4U);
}
bool rv32_address(FILE *out, const char *reg, size_t off) {
    return off <= 2047U ?
        fprintf(out, "  addi %s, sp, %zu\n", reg, off) >= 0 :
        fprintf(out, "  li %s, %zu\n  add %s, sp, %s\n",
                reg, off, reg, reg) >= 0;
}
bool rv32_incoming_word(FILE *out, const Rv32Frame *fr,
                               size_t word, const char *reg) {
    if (word < 8U)
        return fprintf(out, "  mv %s, a%zu\n", reg, word) >= 0;
    return word - 8U <= (SIZE_MAX - fr->frame_size) / 4U &&
           rv32_stack(out, "lw", reg,
                      fr->frame_size + (word - 8U) * 4U);
}
bool rv32_outgoing_word(FILE *out, const Rv32Frame *fr,
                               size_t word, const char *reg) {
    if (word < 8U)
        return fprintf(out, "  mv a%zu, %s\n", word, reg) >= 0;
    return word - 8U < fr->outgoing_size / 4U &&
           rv32_stack(out, "sw", reg, (word - 8U) * 4U);
}
