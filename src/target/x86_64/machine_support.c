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
    /* Common proves an immediately previous scalar GP producer has one
     * semantic consumer. The producer left its normalized value in %rax;
     * x86 does not need the SSA frame reload for that consumer's first GP
     * operand. No other register or input may borrow the forwarding slot. */
    if (frame->gp_forward_source_valid &&
        frame->gp_forward_source_id == id &&
        reg != NULL && strcmp(reg, "%rax") == 0)
        return true;
    return fprintf(out, "  movq -%zu(%%rbp), %s\n", frame->value_offsets[id], reg) >= 0;
}

bool x86_value_store(FILE *out, const MinicX86Frame *frame, MinicCoreValueId id) {
    if (frame == NULL || id >= frame->value_count ||
        frame->value_offsets == NULL) return false;
    /* The producer's instruction has already run. Only its dead SSA result
     * may be omitted, never effects on memory, globals, flags, or calls. */
    if (frame->gp_elide_result_store_valid &&
        frame->gp_elide_result_store_id == id) return true;
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
    char operand[64];
    return out != NULL && reg != NULL &&
           x86_object_memory_operand(frame, id, operand, sizeof(operand)) &&
           fprintf(out, "  leaq %s, %s\n", operand, reg) >= 0;
}

/* One canonical x86 FramePlan addressing formatter for both LEA and native
 * memory operands. Above-16-byte aligned objects MUST use the dynamically
 * aligned RSP base. Never reconstruct byte offsets from Core SSA IDs. */
bool x86_object_memory_operand(const MinicX86Frame *frame,
                               MinicCoreObjectId id, char *out, size_t capacity) {
    int size;
    if (frame == NULL || frame->object_offsets == NULL ||
        id >= frame->object_count || out == NULL || capacity < 2U)
        return false;
    size = frame->objects_from_aligned_rsp
        ? snprintf(out, capacity, "%zu(%%rsp)", frame->object_offsets[id])
        : snprintf(out, capacity, "-%zu(%%rbp)", frame->object_offsets[id]);
    return size > 0 && (size_t)size < capacity;
}

/* Keep x86 width/sign normalization *identical* whether the scalar's memory
 * address lives in R10 or is the proven direct FramePlan effective address. */
bool x86_load_memory_at(FILE *out, size_t bytes, bool sign,
                        const char *operand) {
    const char *opcode, *result;
    if (out == NULL || operand == NULL) return false;
    switch (bytes) {
    case 1U: opcode = sign ? "movsbq" : "movzbq"; result = "%rax"; break;
    case 2U: opcode = sign ? "movswq" : "movzwq"; result = "%rax"; break;
    case 4U: opcode = sign ? "movslq" : "movl"; result = sign ? "%rax" : "%eax"; break;
    case 8U: opcode = "movq"; result = "%rax"; break;
    default: return false;
    }
    return fprintf(out, "  %s %s, %s\n", opcode, operand, result) >= 0;
}

bool x86_store_memory_at(FILE *out, size_t bytes, const char *operand) {
    const char *opcode, *source;
    if (out == NULL || operand == NULL) return false;
    switch (bytes) {
    case 1U: opcode = "movb"; source = "%al"; break;
    case 2U: opcode = "movw"; source = "%ax"; break;
    case 4U: opcode = "movl"; source = "%eax"; break;
    case 8U: opcode = "movq"; source = "%rax"; break;
    default: return false;
    }
    return fprintf(out, "  %s %s, %s\n", opcode, source, operand) >= 0;
}

bool x86_load_memory(FILE *out, size_t bytes, bool sign) {
    return x86_load_memory_at(out, bytes, sign, "(%r10)");
}

bool x86_store_memory(FILE *out, size_t bytes) {
    return x86_store_memory_at(out, bytes, "(%r10)");
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

