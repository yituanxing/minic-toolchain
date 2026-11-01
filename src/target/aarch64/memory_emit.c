#include "target/aarch64/memory_emit.h"
#include "target/aarch64/neon_emit.h"
#include "target/common/direct_instruction_semantics.h"
#include <stdio.h>
#include <stdlib.h>

/* Memory effects use Core-owned spill addresses, never the outgoing CALL
 * area or a live dynamic-alloca SP. Materialize their native A64 operands
 * before output instead of asking the finalizer to parse assembly text. */
static bool memory_slot(FILE *out, const MinicA64FrameView *frame,
                        bool store, MinicA64FrameRegisterClass kind,
                        unsigned int reg, size_t offset) {
    return minic_a64_emit_owned_slot(out, frame, store, kind, reg, offset);
}

/* The full Linux frontier reports failing Core instruction IDs. When a
 * scalar LOAD is rejected, retain the exact source/target type identities so
 * that a new shape is implemented only after its ABI has been established. */
static void report_load_shape_reject(const MinicCoreFunction *fn,
                                     const MinicCoreInstruction *ins,
                                     MinicCoreValueId address) {
    if (getenv("MINIC_BACKEND_CENSUS") != NULL) {
        const MinicType *address_type = address < fn->value_count
            ? &fn->values[address].type : NULL;
        (void)fprintf(stderr,
                      "MINIC_AARCH64_LOAD_SHAPE_REJECT result_base=%d "
                      "result_rank=%d result_ptr=%u result_enum=%zu "
                      "address_id=%zu address_base=%d address_ptr=%u\n",
                      (int)ins->type.base_kind,
                      (int)ins->type.integer_rank,
                      ins->type.pointer_depth, (size_t)ins->type.enum_id,
                      (size_t)address,
                      address_type ? (int)address_type->base_kind : -1,
                      address_type ? address_type->pointer_depth : 0U);
    }
}

static bool emit_integer_memory(FILE *out, bool store,
                                size_t width, bool sign) {
    const char *op = NULL;
    if (store) {
        op = width == 1U ? "  strb w9, [x10]\n" :
             width == 2U ? "  strh w9, [x10]\n" :
             width == 4U ? "  str w9, [x10]\n" :
             width == 8U ? "  str x9, [x10]\n" : NULL;
    } else {
        op = width == 1U ? (sign ? "  ldrsb w9, [x10]\n" :
                                     "  ldrb w9, [x10]\n") :
             width == 2U ? (sign ? "  ldrsh w9, [x10]\n" :
                                     "  ldrh w9, [x10]\n") :
             width == 4U ? "  ldr w9, [x10]\n" :
              width == 8U ? "  ldr x9, [x10]\n" : NULL;
    }
    return op != NULL && fputs(op, out) != EOF;
}


/* Native memory-effect family. Shared Core semantic checks admit only
 * proven scalar and relaxed atomic shapes and complete 16-byte payloads.
 * Scalar reads leave a GP result; wide reads write two SSA words directly. */
bool minic_aarch64_emit_memory_instruction(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    const MinicLP64FramePlan *frame_plan,
    const MinicA64FrameView *frame_view,
    bool *needs_gp_store) {
    if (out == NULL || program == NULL || target == NULL ||
        fn == NULL || ins == NULL || frame_plan == NULL || frame_view == NULL ||
        needs_gp_store == NULL)
        return false;
    *needs_gp_store = false;
    if (ins->kind == MINIC_CORE_INSTRUCTION_FIXED_VECTOR128_LOAD ||
        ins->kind == MINIC_CORE_INSTRUCTION_FIXED_VECTOR128_STORE ||
        ins->kind == MINIC_CORE_INSTRUCTION_FIXED_VECTOR128_XOR ||
        ins->kind == MINIC_CORE_INSTRUCTION_FIXED_VECTOR128_EOR3) {
        MinicType vector_type;
        MinicCoreValueId operand;
        size_t value_slot, result_slot, second_slot;
        if (target->architecture != MINIC_TARGET_ARCH_AARCH64)
            return false;
        if (ins->kind == MINIC_CORE_INSTRUCTION_FIXED_VECTOR128_LOAD) {
            operand = ins->value.load.address;
            if (ins->result >= fn->value_count || operand >= fn->value_count ||
                !minic_aarch64_neon_vector128_type(ins->type) ||
                !minic_lp64_frame_plan_wide_slot(frame_plan, ins->result,
                                                  &result_slot)) return false;
            return minic_aarch64_neon_emit_load(out, frame_view, ins->type,
                minic_lp64_frame_value_offset(frame_plan, operand), result_slot);
        }
        if (ins->kind == MINIC_CORE_INSTRUCTION_FIXED_VECTOR128_STORE) {
            operand = ins->value.store.stored_value;
            if (operand >= fn->value_count ||
                ins->value.store.address >= fn->value_count ||
                !minic_aarch64_neon_vector128_type(fn->values[operand].type) ||
                !minic_lp64_frame_plan_wide_slot(frame_plan, operand,
                                                  &value_slot)) return false;
            return minic_aarch64_neon_emit_store(out, frame_view,
                fn->values[operand].type,
                minic_lp64_frame_value_offset(frame_plan,
                    ins->value.store.address), value_slot);
        }
        operand = ins->kind == MINIC_CORE_INSTRUCTION_FIXED_VECTOR128_XOR
            ? ins->value.binary.left : ins->value.vector_ternary.first;
        const MinicCoreValueId second =
            ins->kind == MINIC_CORE_INSTRUCTION_FIXED_VECTOR128_XOR
                ? ins->value.binary.right : ins->value.vector_ternary.second;
        vector_type = ins->type;
        if (!minic_aarch64_neon_vector128_type(vector_type) ||
            operand >= fn->value_count || second >= fn->value_count ||
            ins->result >= fn->value_count ||
            !minic_lp64_frame_plan_wide_slot(frame_plan, operand, &value_slot) ||
            !minic_lp64_frame_plan_wide_slot(frame_plan, second, &second_slot) ||
            !minic_lp64_frame_plan_wide_slot(frame_plan, ins->result,
                                              &result_slot)) return false;
        if (ins->kind == MINIC_CORE_INSTRUCTION_FIXED_VECTOR128_XOR)
            return minic_aarch64_neon_emit_xor(out, frame_view, vector_type,
                                               value_slot, second_slot,
                                               result_slot);
        if (ins->kind == MINIC_CORE_INSTRUCTION_FIXED_VECTOR128_EOR3) {
            const MinicCoreValueId third = ins->value.vector_ternary.third;
            const MinicCoreInlineAsmId core_source =
                ins->value.vector_ternary.source_inline_asm;
            size_t third_slot;
            size_t source_id;
            if (third >= fn->value_count ||
                core_source >= fn->inline_asm_count ||
                (source_id = fn->inline_asms[core_source].source_inline_asm_id) >=
                    program->inline_asm_count ||
                !minic_aarch64_neon_sha3_eor3_source_form(
                    program, &program->inline_asms[source_id]) ||
                !minic_lp64_frame_plan_wide_slot(frame_plan, third, &third_slot))
                return false;
            /* The original kernel owns the CPU-feature conditional call.
             * This verifies the native ASM *syntax/operands*, not that the
             * caller's execution path observes the SHA3 hardware guard. */
            return minic_aarch64_neon_emit_eor3(out, frame_view, vector_type,
                true, value_slot, second_slot, third_slot, result_slot);
        }
        return false;
    }
        if (ins->kind == MINIC_CORE_INSTRUCTION_STORE &&
            ins->value.store.stored_value < fn->value_count &&
            minic_type_is_int128_integer(
                fn->values[ins->value.store.stored_value].type)) {
            const MinicCoreValueId addr = ins->value.store.address;
            size_t src;
            if (!minic_multi_direct_wide_store_shape(
                    fn, ins, frame_plan, &src) ||
                !memory_slot(out, frame_view, false, MINIC_A64_REG_X, 10U,
                    minic_lp64_frame_value_offset(frame_plan, addr)) ||
                !memory_slot(out, frame_view, false, MINIC_A64_REG_X, 9U, src) ||
                !memory_slot(out, frame_view, false, MINIC_A64_REG_X, 11U,
                    src + 8U) ||
                fputs("  str x9, [x10]\n"
                      "  str x11, [x10, #8]\n", out) == EOF)
                return false;
            return true;
        }
        if (ins->kind == MINIC_CORE_INSTRUCTION_STORE &&
            ins->value.store.stored_value < fn->value_count &&
            minic_type_is_long_double(
                fn->values[ins->value.store.stored_value].type)) {
            const MinicCoreValueId addr = ins->value.store.address;
            size_t src;
            if (!minic_multi_direct_wide_store_shape(
                    fn, ins, frame_plan, &src) ||
                !memory_slot(out, frame_view, false, MINIC_A64_REG_X, 10U,
                    minic_lp64_frame_value_offset(frame_plan, addr)) ||
                !memory_slot(out, frame_view, false, MINIC_A64_REG_Q, 9U, src) ||
                fputs("  str q9, [x10]\n", out) == EOF)
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
            if (!memory_slot(out, frame_view, false, MINIC_A64_REG_X, 10U,
                    minic_lp64_frame_value_offset(frame_plan, addr)) ||
                !memory_slot(out, frame_view, false,
                    (pointer || width == 8U) ? MINIC_A64_REG_X : MINIC_A64_REG_W,
                    9U, minic_lp64_frame_value_offset(frame_plan, value)) ||
                (pointer ? fputs("  str x9, [x10]\n", out) == EOF
                         : !emit_integer_memory(out, true, width, sign))) return false;
            return true;
        }
    switch (ins->kind) {
        case MINIC_CORE_INSTRUCTION_LOAD:
        case MINIC_CORE_INSTRUCTION_ATOMIC_LOAD: {
            if (ins->kind == MINIC_CORE_INSTRUCTION_LOAD &&
                minic_type_is_int128_integer(ins->type)) {
                MinicCoreValueId addr;
                size_t dst;
                if (!minic_multi_direct_wide_scalar_load_location(
                        fn, ins, frame_plan, &addr, &dst)) {
                    report_load_shape_reject(fn, ins, ins->value.load.address);
                    return false;
                }
                if (!memory_slot(out, frame_view, false, MINIC_A64_REG_X, 10U,
                        minic_lp64_frame_value_offset(frame_plan, addr)) ||
                    fputs("  ldr x9, [x10]\n"
                          "  ldr x11, [x10, #8]\n", out) == EOF ||
                    !memory_slot(out, frame_view, true, MINIC_A64_REG_X,
                        9U, dst) ||
                    !memory_slot(out, frame_view, true, MINIC_A64_REG_X,
                        11U, dst + 8U))
                    return false;
                return true;
            }
            if (ins->kind == MINIC_CORE_INSTRUCTION_LOAD &&
                minic_type_is_long_double(ins->type)) {
                MinicCoreValueId addr;
                size_t dst;
                if (!minic_multi_direct_wide_scalar_load_location(
                        fn, ins, frame_plan, &addr, &dst)) {
                    report_load_shape_reject(fn, ins, ins->value.load.address);
                    return false;
                }
                if (!memory_slot(out, frame_view, false, MINIC_A64_REG_X, 10U,
                        minic_lp64_frame_value_offset(frame_plan, addr)) ||
                    fputs("  ldr q9, [x10]\n", out) == EOF ||
                    !memory_slot(out, frame_view, true, MINIC_A64_REG_Q,
                        9U, dst))
                    return false;
                return true;
            }
            const bool atomic = ins->kind == MINIC_CORE_INSTRUCTION_ATOMIC_LOAD;
            const MinicCoreValueId addr = atomic ? ins->value.atomic_load.address
                                                  : ins->value.load.address;
            size_t width = 0U;
            bool sign = false, pointer = false;
            if (!minic_multi_direct_scalar_load_shape(
                    program, target, fn, ins, &width, &sign, &pointer)) {
                report_load_shape_reject(fn, ins, addr);
                return false;
            }
            if (
                !memory_slot(out, frame_view, false, MINIC_A64_REG_X, 10U,
                    minic_lp64_frame_value_offset(frame_plan, addr)) ||
                (pointer ? fputs("  ldr x9, [x10]\n", out) == EOF
                         : !emit_integer_memory(out, false, width, sign)))
                return false;
            *needs_gp_store = true;
            return true;
        }
        default:
            return false;
    }
}
