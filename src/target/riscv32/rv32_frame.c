#include "target/riscv32/rv32_internal.h"
#include "target/riscv32/rv32_abi_cursor.h"
#include "target/common/core_call_view.h"
#include "target/data_layout.h"
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

/* ILP32 frame planner and stack word transport. RV64's LP64-specific
 * dynamic-frame and 8-byte SSA slot assumptions are not imported. */
static bool rv32_align(size_t value, size_t by, size_t *out) {
    if (by == 0U || (by & (by - 1U)) != 0U ||
        value > SIZE_MAX - (by - 1U)) return false;
    *out = (value + (by - 1U)) & ~(by - 1U);
    return true;
}

/* A wide ILP32 operand remains ONE semantic Core SSA value. */
bool rv32_frame_init(Rv32Frame *frame, const MinicC0Program *program,
                            const MinicCoreFunction *f, const MinicTargetInfo *t) {
    size_t next, i, max_stack_args = 0U;
    if (frame == NULL || program == NULL || f == NULL || t == NULL ||
        f->value_count > 100000U || f->object_count > 100000U)
        return false;
    /* Scan the COMPLETE Core call set, not the frontend expression tree.
     * ABI outgoing argument areas are allocated exactly once per function. */
    for (i = 0U; i < f->instruction_count; ++i) {
        const MinicCoreInstruction *ins = &f->instructions[i];
        if (ins->kind == MINIC_CORE_INSTRUCTION_CALL ||
            ins->kind == MINIC_CORE_INSTRUCTION_INDIRECT_CALL) {
            MinicCoreCallView view;
            MinicRiscv32AbiCursor cursor = {0U, 0U, false};
            if (!minic_core_call_view_build(f, ins, &view))
                return false;
            for (size_t a = 0U; a < view.argument_count; ++a) {
                const MinicCoreCallArgument *arg = &view.arguments[a];
                unsigned words;
                MinicRiscv32AbiPlacement loc;
                if (arg->kind != MINIC_CORE_CALL_ARGUMENT_VALUE ||
                    arg->value.value_id >= f->value_count ||
                    !rv32_scalar_storage_words(
                        f, f->values[arg->value.value_id].type, &words) ||
                    !minic_riscv32_abi_place(
                        &cursor, words,
                        view.is_variadic && a >= view.parameter_count, &loc))
                    return false;
            }
            if (cursor.stack_words > max_stack_args)
                max_stack_args = cursor.stack_words;
        }
    }
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
            next > SIZE_MAX - words * 4U) return false;
        frame->value_offsets[i] = next;
        next += words * 4U;
    }
    for (i = 0; i < f->object_count; ++i) {
        const MinicCoreObject *obj = &f->objects[i];
        size_t size, align;
        if (!minic_data_layout_type(t->data_layout, program,
                                    obj->type, &size, &align) ||
            (obj->element_count > 1U &&
             size > SIZE_MAX / obj->element_count)) return false;
        if (obj->element_count > 1U) size *= obj->element_count;
        if (obj->explicit_alignment > align) align = obj->explicit_alignment;
        if (align < 4U) align = 4U;
        if (!rv32_align(next, align, &next) || size > SIZE_MAX - next)
            return false;
        frame->object_offsets[i] = next;
        next += size != 0U ? size : 1U;
    }
    return rv32_align(next, 16U, &frame->frame_size) &&
           frame->frame_size <= (size_t)INT32_MAX;
}
void rv32_frame_destroy(Rv32Frame *f) {
    if (f != NULL) {
        free(f->object_offsets);
        free(f->value_offsets);
        f->object_offsets = NULL;
        f->value_offsets = NULL;
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
