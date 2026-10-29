#include "target/loongarch64/memory_emit.h"
#include "target/common/direct_instruction_semantics.h"
#include "target/loongarch64/frame_emit.h"
#include <stdio.h>

/* A positive proof skips even the alignment branch on the hot path. The
 * Core SSA definition is immutable; only roots whose alignment is known from
 * the target DataLayout are trusted. Unknown/arbitrary pointer provenance
 * returns one and keeps the guarded native fast path. This is intentionally
 * target-local until Core exposes a general AddressAlignmentProof. */
static size_t loong_known_address_alignment(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, MinicCoreValueId value_id,
    unsigned depth) {
    const MinicCoreInstruction *origin;
    size_t size, alignment, base, offset;
    if (program == NULL || target == NULL || fn == NULL ||
        value_id >= fn->value_count || depth > 16U ||
        fn->values[value_id].definition >= fn->instruction_count)
        return 1U;
    origin = &fn->instructions[fn->values[value_id].definition];
    if (origin->result != value_id) return 1U;
    switch (origin->kind) {
        case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS: {
            const MinicCoreObjectId object_id = origin->value.object_id;
            if (object_id >= fn->object_count ||
                !minic_data_layout_type(target->data_layout, program,
                    fn->objects[object_id].type, &size, &alignment))
                return 1U;
            if (fn->objects[object_id].explicit_alignment > alignment)
                alignment = fn->objects[object_id].explicit_alignment;
            return alignment;
        }
        case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS: {
            const MinicCoreGlobalId id = origin->value.global_id;
            return id < fn->global_count &&
                   minic_data_layout_type(target->data_layout, program,
                       fn->globals[id].type, &size, &alignment)
                ? alignment : 1U;
        }
        /* No type-based promise for parameters or reloaded pointers: GNU C
         * can forward &packed.member through an apparently natural T*. */
        case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS:
            base = loong_known_address_alignment(
                program, target, fn, origin->value.field_address.base,
                depth + 1U);
            offset = origin->value.field_address.byte_offset;
            break;
        case MINIC_CORE_INSTRUCTION_POINTER_OFFSET:
            base = loong_known_address_alignment(
                program, target, fn, origin->value.pointer_offset.base,
                depth + 1U);
            offset = origin->value.pointer_offset.element_size;
            break;
        default: return 1U;
    }
    /* gcd(base, offset): (aligned base + arbitrary index*stride) is aligned
     * to this divisor for every runtime index, with no speculation. */
    while (offset != 0U) {
        const size_t remainder = base % offset;
        base = offset;
        offset = remainder;
    }
    return base == 0U ? 1U : base;
}

/* Dynamic frame uses s0; otherwise the frame is SP-relative. */
#define emit_sp_mem(_out, _op, _reg, _offset) \
    minic_loongarch64_emit_frame_mem((_out), (_op), (_reg), (_offset), dynamic_frame)

/* Core LOAD/STORE deliberately carries no trusted alignment metadata: an
 * ordinary scalar or pointer may live inside a GNU packed record. Preserve
 * the native fast path at runtime for aligned addresses, and gather/scatter
 * exactly the requested bytes at unaligned addresses. Atomic accesses retain
 * their hardware-width operation (an unaligned atomic address is invalid). */
static bool emit_integer_memory(FILE *out, bool store,
                                size_t width, bool sign, bool atomic,
                                bool proven_aligned) {
    const char *op = NULL;
    if (store) {
        op = width == 1U ? "  st.b $t0, $t1, 0\n" :
             width == 2U ? "  st.h $t0, $t1, 0\n" :
             width == 4U ? "  st.w $t0, $t1, 0\n" :
             width == 8U ? "  st.d $t0, $t1, 0\n" : NULL;
    } else {
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
    if (store) {
        if (fprintf(out, "  li.d $t4, %zu\n", width) < 0 ||
            fputs("3:\n"
                  "  st.b $t0, $t1, 0\n"
                  "  srli.d $t0, $t0, 8\n"
                  "  addi.d $t1, $t1, 1\n"
                  "  addi.d $t4, $t4, -1\n"
                  "  bnez $t4, 3b\n", out) == EOF)
            return false;
    } else {
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
                program, target, fn, addr, 0U) >= 8U;
            if (!minic_multi_direct_wide_store_shape(
                    fn, ins, frame_plan, &src) ||
                !emit_sp_mem(out, "ld.d", "$t1", (size_t)addr * 8U) ||
                !emit_sp_mem(out, "ld.d", "$t0", src) ||
                !emit_integer_memory(out, true, 8U, false, false, aligned) ||
                !emit_sp_mem(out, "ld.d", "$t1", (size_t)addr * 8U) ||
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
            bool sign = false, pointer = false;
            if (!minic_multi_direct_scalar_store_shape(
                    program, target, fn, ins, &width, &sign, &pointer))
                return false;
            if (!emit_sp_mem(out, "ld.d", "$t1", (size_t)addr * 8U) ||
                !emit_sp_mem(out, (pointer || width == 8U) ? "ld.d" : "ld.w", "$t0",
                             (size_t)value * 8U) ||
                !emit_integer_memory(out, true, pointer ? 8U : width, sign,
                                     atomic,
                     loong_known_address_alignment(program, target, fn,
                         addr, 0U) >= (pointer ? 8U : width))) return false;
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
                    !emit_sp_mem(out, "ld.d", "$t1", (size_t)addr * 8U))
                    return false;
                aligned = loong_known_address_alignment(
                    program, target, fn, addr, 0U) >= 8U;
                if (!emit_integer_memory(out, false, 8U, false, false, aligned) ||
                    !emit_sp_mem(out, "st.d", "$t0", dst) ||
                    !emit_sp_mem(out, "ld.d", "$t1", (size_t)addr * 8U) ||
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
            bool sign = false, pointer = false;
            if (!minic_multi_direct_scalar_load_shape(
                    program, target, fn, ins, &width, &sign, &pointer))
                return false;
            if (
                !emit_sp_mem(out, "ld.d", "$t1", (size_t)addr * 8U) ||
                !emit_integer_memory(out, false, pointer ? 8U : width, sign,
                                     atomic,
                     loong_known_address_alignment(program, target, fn,
                         addr, 0U) >= (pointer ? 8U : width)))
                return false;
            *needs_gp_store = true;
            return true;
        }
        default:
            return false;
    }
}

#undef emit_sp_mem
