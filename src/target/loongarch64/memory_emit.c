#include "target/loongarch64/memory_emit.h"
#include "core/address_alignment.h"
#include "target/common/direct_instruction_semantics.h"
#include "target/loongarch64/frame_emit.h"
#include <stdio.h>
#include <string.h>

/* Core owns the recursive proof and gcd propagation. Preserve the former
 * Loong 16-edge proof budget and its existing DataLayout root policy,
 * including GLOBAL_ADDRESS (RV deliberately has a different global policy).
 * Unknown/arbitrary pointer provenance still keeps the guarded native path. */
typedef struct LoongAddressAlignmentContext {
    const MinicC0Program *program;
    const MinicTargetInfo *target;
} LoongAddressAlignmentContext;

static size_t loong_address_root_alignment(
    const MinicCoreFunction *fn, const MinicCoreInstruction *origin,
    void *opaque) {
    const LoongAddressAlignmentContext *ctx = opaque;
    size_t size, alignment;
    if (ctx == NULL || ctx->program == NULL || ctx->target == NULL ||
        ctx->target->data_layout == NULL || fn == NULL || origin == NULL)
        return 1U;
    switch (origin->kind) {
    case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS: {
        const MinicCoreObjectId id = origin->value.object_id;
        if (id >= fn->object_count || fn->objects == NULL ||
            !minic_data_layout_type(ctx->target->data_layout, ctx->program,
                fn->objects[id].type, &size, &alignment)) return 1U;
        if (fn->objects[id].explicit_alignment > alignment)
            alignment = fn->objects[id].explicit_alignment;
        return alignment;
    }
    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS: {
        const MinicCoreGlobalId id = origin->value.global_id;
        return id < fn->global_count && fn->globals != NULL &&
               minic_data_layout_type(ctx->target->data_layout, ctx->program,
                   fn->globals[id].type, &size, &alignment)
            ? alignment : 1U;
    }
    default: return 1U;
    }
}

/* Retain the Core identity of an exact frame-object address
 * through constant field projections. This is a semantic proof (not a
 * textual ASM peephole): an ADDRESS whose origin is a frame object cannot
 * name an arbitrary user pointer or a different allocation. */
static bool loong_static_frame_address(
    const MinicCoreFunction *fn, const MinicLP64FramePlan *frame_plan,
    MinicCoreValueId value_id, unsigned depth, size_t *offset) {
    const MinicCoreInstruction *origin;
    size_t base, extra;
    if (fn == NULL || frame_plan == NULL || offset == NULL ||
        value_id >= fn->value_count || depth > 16U ||
        fn->values[value_id].definition >= fn->instruction_count)
        return false;
    origin = &fn->instructions[fn->values[value_id].definition];
    if (origin->result != value_id) return false;
    if (origin->kind == MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS)
        return minic_lp64_frame_plan_object(
            frame_plan, origin->value.object_id, offset);
    if (origin->kind != MINIC_CORE_INSTRUCTION_FIELD_ADDRESS ||
        !loong_static_frame_address(
            fn, frame_plan, origin->value.field_address.base,
            depth + 1U, &base))
        return false;
    extra = origin->value.field_address.byte_offset;
    if (base > MINIC_LARGE_FRAME_MAX ||
        extra > MINIC_LARGE_FRAME_MAX - base)
        return false;
    *offset = base + extra;
    return true;
}

static const char *loong_frame_load_op(size_t width, bool sign) {
    switch (width) {
    case 1U: return sign ? "ld.b" : "ld.bu";
    case 2U: return sign ? "ld.h" : "ld.hu";
    case 4U: return "ld.w";
    case 8U: return "ld.d";
    default: return NULL;
    }
}

static const char *loong_frame_store_op(size_t width) {
    switch (width) {
    case 1U: return "st.b";
    case 2U: return "st.h";
    case 4U: return "st.w";
    case 8U: return "st.d";
    default: return NULL;
    }
}

/* A positive proof skips even the alignment branch on the hot path. The
 * shared Core alignment proof keeps the target's DataLayout root policy;
 * unknown/arbitrary pointers retain the original guarded access path. */
static size_t loong_known_address_alignment(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, MinicCoreValueId value_id) {
    LoongAddressAlignmentContext context = {program, target};
    return minic_core_address_known_alignment_bounded(
        fn, value_id, loong_address_root_alignment, &context, 16U);
}

bool minic_loongarch64_direct_frame_memory_use(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicLP64FramePlan *plan,
    const MinicCoreInstruction *ins) {
    MinicCoreValueId address;
    size_t width = 0U, direct_offset = 0U;
    bool sign = false, pointer = false;
    if (program == NULL || target == NULL || fn == NULL ||
        plan == NULL || ins == NULL)
        return false;
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_STORE:
    case MINIC_CORE_INSTRUCTION_ATOMIC_STORE:
        address = ins->kind == MINIC_CORE_INSTRUCTION_STORE
            ? ins->value.store.address : ins->value.atomic_store.address;
        if ((ins->kind == MINIC_CORE_INSTRUCTION_STORE &&
             ins->value.store.stored_value >= fn->value_count) ||
            (ins->kind == MINIC_CORE_INSTRUCTION_STORE &&
             (minic_type_is_int128_integer(
                 fn->values[ins->value.store.stored_value].type) ||
              minic_type_is_long_double(
                 fn->values[ins->value.store.stored_value].type))) ||
            !minic_multi_direct_scalar_store_shape(
                program, target, fn, ins, &width, &sign, &pointer))
            return false;
        break;
    case MINIC_CORE_INSTRUCTION_LOAD:
    case MINIC_CORE_INSTRUCTION_ATOMIC_LOAD:
        address = ins->kind == MINIC_CORE_INSTRUCTION_LOAD
            ? ins->value.load.address : ins->value.atomic_load.address;
        if ((ins->kind == MINIC_CORE_INSTRUCTION_LOAD &&
             (minic_type_is_int128_integer(ins->type) ||
              minic_type_is_long_double(ins->type))) ||
            !minic_multi_direct_scalar_load_shape(
                program, target, fn, ins, &width, &sign, &pointer))
            return false;
        break;
    default: return false;
    }
    width = pointer ? 8U : width;
    return loong_known_address_alignment(
               program, target, fn, address) >= width &&
           loong_static_frame_address(
               fn, plan, address, 0U, &direct_offset);
}

/* Dynamic frame uses s0; otherwise the frame is SP-relative. */
#define emit_sp_mem(_out, _op, _reg, _offset) \
    minic_loongarch64_emit_frame_mem((_out), (_op), (_reg), (_offset), dynamic_frame)

/* A single transport for compiler-generated integer stores, including the
 * outputs of target-qualified GNU ASM. The ASM instruction itself is left
 * untouched: only the compiler's writeback to its C lvalue is guarded. */
static bool loong_emit_scalar_store(FILE *out, size_t width,
                                    const char *value_reg,
                                    const char *address_reg,
                                    const char *scratch_reg,
                                    bool proven_aligned) {
    static size_t serial;
    const char *op = width == 1U ? "st.b" : width == 2U ? "st.h" :
                     width == 4U ? "st.w" : width == 8U ? "st.d" : NULL;
    size_t i, label;
    if (!out || !op || !value_reg || !address_reg || !scratch_reg ||
        strcmp(value_reg, address_reg) == 0 ||
        strcmp(value_reg, scratch_reg) == 0 ||
        strcmp(address_reg, scratch_reg) == 0)
        return false;
    if (width == 1U || proven_aligned)
        return fprintf(out, "  %s %s, %s, 0\n", op,
                       value_reg, address_reg) >= 0;
    label = serial++;
    if (fprintf(out,
            "  andi %s, %s, %zu\n"
            "  beqz %s, .Lloong_store_native_%zu\n"
            "  move %s, %s\n",
            scratch_reg, address_reg, width - 1U,
            scratch_reg, label, scratch_reg, value_reg) < 0)
        return false;
    for (i = 0U; i < width; ++i) {
        if (fprintf(out, "  st.b %s, %s, %zu\n",
                    scratch_reg, address_reg, i) < 0 ||
            (i + 1U < width &&
             fprintf(out, "  srli.d %s, %s, 8\n",
                     scratch_reg, scratch_reg) < 0)) return false;
    }
    return fprintf(out,
            "  b .Lloong_store_done_%zu\n"
            ".Lloong_store_native_%zu:\n"
            "  %s %s, %s, 0\n"
            ".Lloong_store_done_%zu:\n",
            label, label, op, value_reg, address_reg, label) >= 0;
}

bool minic_loongarch64_emit_asm_output_store(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, MinicCoreValueId address,
    size_t width, const char *value_reg, const char *address_reg,
    const char *scratch_reg) {
    return loong_emit_scalar_store(out, width, value_reg, address_reg,
        scratch_reg, loong_known_address_alignment(program, target, fn,
            address) >= width);
}

/* Core LOAD/STORE deliberately carries no trusted alignment metadata: an
 * ordinary scalar or pointer may live inside a GNU packed record. Preserve
 * the native fast path at runtime for aligned addresses, and gather/scatter
 * exactly the requested bytes at unaligned addresses. Atomic accesses retain
 * their hardware-width operation (an unaligned atomic address is invalid). */
static bool emit_integer_memory(FILE *out, bool store,
                                size_t width, bool sign, bool atomic,
                                bool proven_aligned) {
    const char *op = NULL;
    if (store)
        return loong_emit_scalar_store(out, width, "$t0", "$t1", "$t3",
                                       atomic || proven_aligned);
    else {
        op = width == 1U ? (sign ? "  ld.b $t0, $t1, 0\n" :
                                     "  ld.bu $t0, $t1, 0\n") :
             width == 2U ? (sign ? "  ld.h $t0, $t1, 0\n" :
                                     "  ld.hu $t0, $t1, 0\n") :
             width == 4U ? "  ld.w $t0, $t1, 0\n" :
              width == 8U ? "  ld.d $t0, $t1, 0\n" : NULL;
    }
    if (op == NULL) return false;
    if (width == 1U || atomic || proven_aligned)
        return fputs(op, out) != EOF;
    if (fprintf(out, "  andi $t3, $t1, %zu\n"
                     "  bnez $t3, 1f\n", width - 1U) < 0 ||
        fputs(op, out) == EOF ||
        fputs("  b 2f\n1:\n", out) == EOF)
        return false;
    {
        if (fprintf(out, "  li.d $t4, %zu\n", width) < 0 ||
            fputs("  move $t0, $zero\n"
                  "  move $t5, $zero\n"
                  "3:\n"
                  "  ld.bu $t2, $t1, 0\n"
                  "  sll.d $t2, $t2, $t5\n"
                  "  or $t0, $t0, $t2\n"
                  "  addi.d $t1, $t1, 1\n"
                  "  addi.d $t5, $t5, 8\n"
                  "  addi.d $t4, $t4, -1\n"
                  "  bnez $t4, 3b\n", out) == EOF)
            return false;
        /* Native ld.w sign-extends to 64 bits even for unsigned C int.
         * Narrow signed integers need the same sign extension as ld.h. */
        if (width == 4U && fputs("  slli.w $t0, $t0, 0\n", out) == EOF)
            return false;
        if (width == 2U && sign &&
            fputs("  slli.d $t0, $t0, 48\n"
                  "  srai.d $t0, $t0, 48\n", out) == EOF)
            return false;
    }
    return fputs("2:\n", out) != EOF;
}

bool minic_loongarch64_emit_asm_input_scalar(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, MinicCoreValueId address, size_t width) {
    if (!out || !program || !target || !fn || address >= fn->value_count ||
        (width != 4U && width != 8U))
        return false;
    /* Both native ld.w and byte-gather must extend the word to the same
     * 64-bit GPR representation; ld.d transports the entire wide carrier. */
    return emit_integer_memory(out, false, width, width == 4U, false,
        loong_known_address_alignment(program, target, fn,
                                      address) >= width);
}


/* Native memory-effect family. Shared Core semantic checks admit only
 * proven scalar and relaxed atomic shapes and complete 16-byte payloads.
 * Scalar reads leave a GP result; wide reads write two SSA words directly. */
bool minic_loongarch64_emit_memory_instruction(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    const MinicLP64FramePlan *frame_plan,
    bool dynamic_frame, bool *needs_gp_store) {
    if (out == NULL || program == NULL || target == NULL ||
        fn == NULL || ins == NULL || frame_plan == NULL ||
        needs_gp_store == NULL)
        return false;
    *needs_gp_store = false;
        if (ins->kind == MINIC_CORE_INSTRUCTION_STORE &&
            ins->value.store.stored_value < fn->value_count &&
            (minic_type_is_int128_integer(
                fn->values[ins->value.store.stored_value].type) ||
             minic_type_is_long_double(
                 fn->values[ins->value.store.stored_value].type))) {
            const MinicCoreValueId addr = ins->value.store.address;
            size_t src;
            const bool aligned = loong_known_address_alignment(
                program, target, fn, addr) >= 8U;
            if (!minic_multi_direct_wide_store_shape(
                    fn, ins, frame_plan, &src) ||
                !emit_sp_mem(out, "ld.d", "$t1", minic_lp64_frame_value_offset(frame_plan, addr)) ||
                !emit_sp_mem(out, "ld.d", "$t0", src) ||
                !emit_integer_memory(out, true, 8U, false, false, aligned) ||
                !emit_sp_mem(out, "ld.d", "$t1", minic_lp64_frame_value_offset(frame_plan, addr)) ||
                fputs("  addi.d $t1, $t1, 8\n", out) == EOF ||
                !emit_sp_mem(out, "ld.d", "$t0", src + 8U) ||
                !emit_integer_memory(out, true, 8U, false, false, aligned))
                return false;
            return true;
        }
        if (ins->kind == MINIC_CORE_INSTRUCTION_STORE ||
            ins->kind == MINIC_CORE_INSTRUCTION_ATOMIC_STORE) {
            const bool atomic = ins->kind == MINIC_CORE_INSTRUCTION_ATOMIC_STORE;
            const MinicCoreValueId addr = atomic ? ins->value.atomic_store.address
                                                  : ins->value.store.address;
            const MinicCoreValueId value = atomic ? ins->value.atomic_store.stored_value
                                                   : ins->value.store.stored_value;
            size_t width = 0U;
            size_t direct_offset = 0U;
            bool sign = false, pointer = false;
            if (!minic_multi_direct_scalar_store_shape(
                    program, target, fn, ins, &width, &sign, &pointer))
                return false;
            if (minic_loongarch64_direct_frame_memory_use(
                    program, target, fn, frame_plan, ins)) {
                const char *op = loong_frame_store_op(pointer ? 8U : width);
                if (op == NULL ||
                    !loong_static_frame_address(
                        fn, frame_plan, addr, 0U, &direct_offset) ||
                    !emit_sp_mem(out, (pointer || width == 8U) ? "ld.d" : "ld.w",
                        "$t0", minic_lp64_frame_value_offset(frame_plan, value)) ||
                    !emit_sp_mem(out, op, "$t0", direct_offset))
                    return false;
                return true;
            }
            if (!emit_sp_mem(out, "ld.d", "$t1", minic_lp64_frame_value_offset(frame_plan, addr)) ||
                !emit_sp_mem(out, (pointer || width == 8U) ? "ld.d" : "ld.w", "$t0",
                             minic_lp64_frame_value_offset(frame_plan, value)) ||
                !emit_integer_memory(out, true, pointer ? 8U : width, sign,
                                     atomic,
                     loong_known_address_alignment(program, target, fn,
                         addr) >= (pointer ? 8U : width))) return false;
            return true;
        }
    switch (ins->kind) {
        case MINIC_CORE_INSTRUCTION_LOAD:
        case MINIC_CORE_INSTRUCTION_ATOMIC_LOAD: {
            if (ins->kind == MINIC_CORE_INSTRUCTION_LOAD &&
                (minic_type_is_int128_integer(ins->type) ||
                 minic_type_is_long_double(ins->type))) {
                MinicCoreValueId addr;
                size_t dst;
                bool aligned;
                if (!minic_multi_direct_wide_scalar_load_location(
                        fn, ins, frame_plan, &addr, &dst) ||
                    !emit_sp_mem(out, "ld.d", "$t1", minic_lp64_frame_value_offset(frame_plan, addr)))
                    return false;
                aligned = loong_known_address_alignment(
                    program, target, fn, addr) >= 8U;
                if (!emit_integer_memory(out, false, 8U, false, false, aligned) ||
                    !emit_sp_mem(out, "st.d", "$t0", dst) ||
                    !emit_sp_mem(out, "ld.d", "$t1", minic_lp64_frame_value_offset(frame_plan, addr)) ||
                    fputs("  addi.d $t1, $t1, 8\n", out) == EOF ||
                    !emit_integer_memory(out, false, 8U, false, false, aligned) ||
                    !emit_sp_mem(out, "st.d", "$t0", dst + 8U))
                    return false;
                return true;
            }
            const bool atomic = ins->kind == MINIC_CORE_INSTRUCTION_ATOMIC_LOAD;
            const MinicCoreValueId addr = atomic ? ins->value.atomic_load.address
                                                  : ins->value.load.address;
            size_t width = 0U;
            size_t direct_offset = 0U;
            bool sign = false, pointer = false;
            if (!minic_multi_direct_scalar_load_shape(
                    program, target, fn, ins, &width, &sign, &pointer))
                return false;
            if (minic_loongarch64_direct_frame_memory_use(
                    program, target, fn, frame_plan, ins)) {
                const char *op = loong_frame_load_op(pointer ? 8U : width, sign);
                if (op == NULL ||
                    !loong_static_frame_address(
                        fn, frame_plan, addr, 0U, &direct_offset) ||
                    !emit_sp_mem(out, op, "$t0", direct_offset))
                    return false;
                *needs_gp_store = true;
                return true;
            }
            if (
                !emit_sp_mem(out, "ld.d", "$t1", minic_lp64_frame_value_offset(frame_plan, addr)) ||
                !emit_integer_memory(out, false, pointer ? 8U : width, sign,
                                     atomic,
                     loong_known_address_alignment(program, target, fn,
                         addr) >= (pointer ? 8U : width)))
                return false;
            *needs_gp_store = true;
            return true;
        }
        default:
            return false;
    }
}

#undef emit_sp_mem
