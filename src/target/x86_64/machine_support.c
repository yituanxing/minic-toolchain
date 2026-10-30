#include "target/x86_64/machine_internal.h"
#include "target/x86_64/abi.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Machine-local primitives used by the selected native instruction family.
 * Keep the syntax/registers architecture-owned; Core IDs map to the same
 * already-validated frame slots regardless of instruction family. */
bool x86_one_gp_load(FILE *out, size_t bytes) {
    switch (bytes) {
    case 1U: return fprintf(out, "  movzbq 0(%%r11), %%rax\n") >= 0;
    case 2U: return fprintf(out, "  movzwq 0(%%r11), %%rax\n") >= 0;
    case 4U: return fprintf(out, "  movl 0(%%r11), %%eax\n") >= 0;
    case 8U: return fprintf(out, "  movq 0(%%r11), %%rax\n") >= 0;
    default: return false;
    }
}

bool x86_one_gp_store(FILE *out, size_t bytes) {
    switch (bytes) {
    case 1U: return fprintf(out, "  movb %%al, 0(%%r10)\n") >= 0;
    case 2U: return fprintf(out, "  movw %%ax, 0(%%r10)\n") >= 0;
    case 4U: return fprintf(out, "  movl %%eax, 0(%%r10)\n") >= 0;
    case 8U: return fprintf(out, "  movq %%rax, 0(%%r10)\n") >= 0;
    default: return false;
    }
}





bool x86_normalize(FILE *out, const MinicC0Program *program,
                          const MinicTargetInfo *target, MinicType type) {
    size_t bytes;
    bool sign;
    if (!minic_x86_64_scalar_width(program, target, type, &bytes, &sign)) return false;
    if (minic_type_is_bool_integer(type))
        return fprintf(out, "  testq %%rax, %%rax\n  setne %%al\n  movzbq %%al, %%rax\n") >= 0;
    switch (bytes) {
    case 1U: return fprintf(out, sign ? "  movsbq %%al, %%rax\n" : "  movzbq %%al, %%rax\n") >= 0;
    case 2U: return fprintf(out, sign ? "  movswq %%ax, %%rax\n" : "  movzwq %%ax, %%rax\n") >= 0;
    case 4U: return fprintf(out, sign ? "  movslq %%eax, %%rax\n" : "  movl %%eax, %%eax\n") >= 0;
    case 8U: return true;
    }
    return false;
}

bool x86_value_load(FILE *out, const MinicX86Frame *frame, MinicCoreValueId id,
                           const char *reg) {
    if (frame == NULL || id >= frame->value_count ||
        frame->value_offsets == NULL) return false;
    return fprintf(out, "  movq -%zu(%%rbp), %s\n", frame->value_offsets[id], reg) >= 0;
}

bool x86_value_store(FILE *out, const MinicX86Frame *frame, MinicCoreValueId id) {
    if (frame == NULL || id >= frame->value_count ||
        frame->value_offsets == NULL) return false;
    /* The producer's instruction has already run. Only its dead SSA result
     * may be omitted, never effects on memory, globals, flags, or calls. */
    if (frame->function_value_use_counts != NULL &&
        frame->function_value_use_counts[id] == 0U) return true;
    return fprintf(out, "  movq %%rax, -%zu(%%rbp)\n", frame->value_offsets[id]) >= 0;
}

/* GCC -mindirect-branch=thunk-inline retpoline, with an explicit register
 * target: the outer CALL keeps the real caller continuation on the stack;
 * the inner CALL traps speculative returns until the setup RET replaces its
 * prediction target. For computed goto, the outer operation is a JMP, so
 * the final target receives an unchanged stack pointer. Numeric GAS labels
 * are intentionally local to each contiguous emitted thunk instance. */
bool x86_emit_indirect_inline_thunk(FILE *out, const char *target_register,
                                    bool is_call) {
    if (out == NULL || target_register == NULL ||
        (strcmp(target_register, "%rax") != 0 &&
         strcmp(target_register, "%r11") != 0)) return false;
    return fprintf(out,
        "  jmp 4f\n"
        "1:\n"
        "  call 3f\n"
        "2:\n"
        "  pause\n"
        "  lfence\n"
        "  jmp 2b\n"
        "3:\n"
        "  movq %s, (%%rsp)\n"
        "  ret\n"
        "4:\n"
        "  %s 1b\n", target_register, is_call ? "call" : "jmp") >= 0;
}

/* Low limb at -offset(%rbp), high limb at -offset+8(%rbp). */
/* An x86 SysV long double is a 10-byte x87 extended value in a
 * 16-byte memory slot, NOT IEEE binary128/TFmode. */
bool x86_x87_load(FILE *out, const MinicX86Frame *frame, MinicCoreValueId id) {
    if (out == NULL || frame == NULL || frame->value_offsets == NULL ||
        id >= frame->value_count || frame->value_offsets[id] < 16U) return false;
    return fprintf(out, "  fldt -%zu(%%rbp)\n", frame->value_offsets[id]) >= 0;
}

bool x86_x87_store(FILE *out, const MinicX86Frame *frame, MinicCoreValueId id) {
    if (out == NULL || frame == NULL || frame->value_offsets == NULL ||
        id >= frame->value_count || frame->value_offsets[id] < 16U) return false;
    return fprintf(out, "  fstpt -%zu(%%rbp)\n", frame->value_offsets[id]) >= 0;
}

bool x86_i128_load(FILE *out, const MinicX86Frame *frame, MinicCoreValueId id,
                          const char *low, const char *high) {
    size_t offset;
    if (frame == NULL || id >= frame->value_count ||
        frame->value_offsets == NULL || low == NULL || high == NULL) return false;
    offset = frame->value_offsets[id];
    if (offset < 16U) return false;
    return fprintf(out,
        "  movq -%zu(%%rbp), %s\n"
        "  movq -%zu(%%rbp), %s\n", offset, low, offset - 8U, high) >= 0;
}

bool x86_i128_store(FILE *out, const MinicX86Frame *frame, MinicCoreValueId id,
                           const char *low, const char *high) {
    size_t offset;
    if (frame == NULL || id >= frame->value_count ||
        frame->value_offsets == NULL || low == NULL || high == NULL) return false;
    offset = frame->value_offsets[id];
    if (offset < 16U) return false;
    if (frame->function_value_use_counts != NULL &&
        frame->function_value_use_counts[id] == 0U) return true;
    return fprintf(out,
        "  movq %s, -%zu(%%rbp)\n"
        "  movq %s, -%zu(%%rbp)\n", low, offset, high, offset - 8U) >= 0;
}

bool x86_object_address(FILE *out, const MinicX86Frame *frame,
                               MinicCoreObjectId id, const char *reg) {
    if (out == NULL || frame == NULL || id >= frame->object_count || reg == NULL)
        return false;
    return frame->objects_from_aligned_rsp
        ? fprintf(out, "  leaq %zu(%%rsp), %s\n", frame->object_offsets[id], reg) >= 0
        : fprintf(out, "  leaq -%zu(%%rbp), %s\n", frame->object_offsets[id], reg) >= 0;
}

bool x86_load_memory(FILE *out, size_t bytes, bool sign) {
    switch (bytes) {
    case 1U: return fprintf(out, sign ? "  movsbq (%%r10), %%rax\n" : "  movzbq (%%r10), %%rax\n") >= 0;
    case 2U: return fprintf(out, sign ? "  movswq (%%r10), %%rax\n" : "  movzwq (%%r10), %%rax\n") >= 0;
    case 4U: return fprintf(out, sign ? "  movslq (%%r10), %%rax\n" : "  movl (%%r10), %%eax\n") >= 0;
    case 8U: return fprintf(out, "  movq (%%r10), %%rax\n") >= 0;
    }
    return false;
}

bool x86_store_memory(FILE *out, size_t bytes) {
    switch (bytes) {
    case 1U: return fprintf(out, "  movb %%al, (%%r10)\n") >= 0;
    case 2U: return fprintf(out, "  movw %%ax, (%%r10)\n") >= 0;
    case 4U: return fprintf(out, "  movl %%eax, (%%r10)\n") >= 0;
    case 8U: return fprintf(out, "  movq %%rax, (%%r10)\n") >= 0;
    }
    return false;
}

/* cJSON's static error/hook tables require initialized records containing
 * pointers to objects and functions. Keep this as a target-owned simple-record
 * emitter; nested aggregates and bit-fields still fail closed. */
/* SysV uses independent integer and SSE register sequences.  Both direct
 * and indirect calls share this small classification/placement routine; an
 * unsupported argument class or register overflow fails instead of being
 * silently passed through the wrong register bank. */
/* SysV has separate six-GPR and eight-SSE argument banks; exhausted bank
 * values are stack-passed in source order.  This is the existing scalar ABI,
 * with the separately validated one-GP/two-GP aggregate shapes below. */

