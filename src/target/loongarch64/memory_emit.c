#include "target/loongarch64/memory_emit.h"
#include "target/common/direct_instruction_semantics.h"
#include "target/loongarch64/frame_emit.h"
#include <stdio.h>

/* Dynamic frame uses s0; otherwise the frame is SP-relative. */
#define emit_sp_mem(_out, _op, _reg, _offset) \
    minic_loongarch64_emit_frame_mem((_out), (_op), (_reg), (_offset), dynamic_frame)

static bool emit_integer_memory(FILE *out, bool store,
                                size_t width, bool sign) {
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
    return op != NULL && fputs(op, out) != EOF;
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
            if (!minic_multi_direct_wide_store_shape(
                    fn, ins, frame_plan, &src) ||
                !emit_sp_mem(out, "ld.d", "$t1", (size_t)addr * 8U) ||
                !emit_sp_mem(out, "ld.d", "$t0", src) ||
                fputs("  st.d $t0, $t1, 0\n", out) == EOF ||
                !emit_sp_mem(out, "ld.d", "$t0", src + 8U) ||
                fputs("  st.d $t0, $t1, 8\n", out) == EOF)
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
                (pointer ? fputs("  st.d $t0, $t1, 0\n", out) == EOF
                         : !emit_integer_memory(out, true, width, sign))) return false;
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
                if (!minic_multi_direct_wide_scalar_load_location(
                        fn, ins, frame_plan, &addr, &dst) ||
                !emit_sp_mem(out, "ld.d", "$t1", (size_t)addr * 8U) ||
                    fputs("  ld.d $t0, $t1, 0\n", out) == EOF ||
                    !emit_sp_mem(out, "st.d", "$t0", dst) ||
                    fputs("  ld.d $t0, $t1, 8\n", out) == EOF ||
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
                (pointer ? fputs("  ld.d $t0, $t1, 0\n", out) == EOF
                         : !emit_integer_memory(out, false, width, sign)))
                return false;
            *needs_gp_store = true;
            return true;
        }
        default:
            return false;
    }
}

#undef emit_sp_mem
