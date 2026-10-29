#include "target/aarch64/integer_emit.h"
#include "target/aarch64/scalar_normalize.h"
#include "target/common/direct_instruction_semantics.h"
#include "target/common/core_numeric_shape.h"
#include "target/common/lp64_scalar.h"
#include <stdio.h>

/* Core integer/bit/pointer ALU instruction family. Operand shape/width
 * admission is shared; native opcodes, overflow flags and pair shifts
 * are target-owned. Scalar results use the caller's GP SSA writeback,
 * while 128-bit shifts directly write their two frame-slot halves. */
bool minic_aarch64_emit_integer_instruction(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    const MinicLP64FramePlan *frame_plan,
    bool *needs_gp_store) {
    if (out == NULL || program == NULL || target == NULL ||
        fn == NULL || ins == NULL || frame_plan == NULL ||
         needs_gp_store == NULL)
        return false;
    *needs_gp_store = false;
    /* Wide Core values are always two consecutive 64-bit frame limbs.
     * Keep carry/borrow, signed high-word ordering and source-pair
     * validation here, independent of the AAPCS64 call classifier. */
    if ((ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_NEGATE ||
         ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT) &&
        minic_type_is_int128_integer(ins->type)) {
        const MinicCoreValueId src = ins->value.operand;
        size_t from, to;
        if (src >= fn->value_count ||
            !minic_type_equal(fn->values[src].type, ins->type) ||
            !minic_lp64_frame_plan_wide_slot(frame_plan, src, &from) ||
            !minic_lp64_frame_plan_wide_slot(frame_plan, ins->result, &to) ||
            fprintf(out, "  ldr x9, [sp, #%zu]\n"
                         "  ldr x10, [sp, #%zu]\n",
                    from, from + 8U) < 0 ||
            fputs(ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_NEGATE
                  ? "  negs x9, x9\n  ngc x10, x10\n"
                  : "  mvn x9, x9\n  mvn x10, x10\n", out) == EOF ||
            fprintf(out, "  str x9, [sp, #%zu]\n"
                         "  str x10, [sp, #%zu]\n", to, to + 8U) < 0)
            return false;
        return true;
    }
    if (ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_ADD ||
        ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT ||
        ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY ||
        ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND ||
        ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR ||
        ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR ||
        ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_LESS ||
        ins->kind == MINIC_CORE_INSTRUCTION_SCALAR_EQUAL) {
        const MinicCoreValueId lhs = ins->value.binary.left;
        const MinicCoreValueId rhs = ins->value.binary.right;
        const bool comparison =
            ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_LESS ||
            ins->kind == MINIC_CORE_INSTRUCTION_SCALAR_EQUAL;
        if (lhs < fn->value_count && rhs < fn->value_count &&
            minic_type_is_int128_integer(fn->values[lhs].type)) {
            size_t left, right, dst = 0U;
            const bool sign = minic_type_is_signed_integer(fn->values[lhs].type);
            const char *op = NULL;
            if (!minic_type_equal(fn->values[lhs].type, fn->values[rhs].type) ||
                (!comparison &&
                 !minic_type_equal(ins->type, fn->values[lhs].type)) ||
                !minic_lp64_frame_plan_wide_slot(frame_plan, lhs, &left) ||
                !minic_lp64_frame_plan_wide_slot(frame_plan, rhs, &right) ||
                (!comparison &&
                 !minic_lp64_frame_plan_wide_slot(frame_plan, ins->result, &dst)) ||
                fprintf(out, "  ldr x9, [sp, #%zu]\n"
                             "  ldr x10, [sp, #%zu]\n"
                             "  ldr x11, [sp, #%zu]\n"
                             "  ldr x13, [sp, #%zu]\n",
                        left, left + 8U, right, right + 8U) < 0)
                return false;
            switch (ins->kind) {
            case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
                op = "  adds x9, x9, x11\n  adc x10, x10, x13\n";
                break;
            case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
                op = "  subs x9, x9, x11\n  sbc x10, x10, x13\n";
                break;
            case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY:
                /* 128-bit product modulo 2^128. The low half is A0*B0;
                 * high half is umulh(A0,B0) + A0*B1 + A1*B0.
                 * This bit pattern is correct for signed and unsigned C
                 * int128 whenever signed multiplication is defined. */
                op = "  umulh x14, x9, x11\n"
                     "  madd x14, x9, x13, x14\n"
                     "  madd x10, x10, x11, x14\n"
                     "  mul x9, x9, x11\n";
                break;
            case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:
                op = "  and x9, x9, x11\n  and x10, x10, x13\n";
                break;
            case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:
                op = "  eor x9, x9, x11\n  eor x10, x10, x13\n";
                break;
            case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:
                op = "  orr x9, x9, x11\n  orr x10, x10, x13\n";
                break;
            case MINIC_CORE_INSTRUCTION_INTEGER_LESS:
                op = sign
                    ? "  cmp x10, x13\n  cset w14, lt\n  cset w15, eq\n"
                    : "  cmp x10, x13\n  cset w14, lo\n  cset w15, eq\n";
                break;
            case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:
                op = "  eor x9, x9, x11\n"
                     "  eor x10, x10, x13\n"
                     "  orr x9, x9, x10\n"
                     "  cmp x9, #0\n  cset w9, eq\n";
                break;
            default: return false;
            }
            if (fputs(op, out) == EOF) return false;
            if (ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_LESS) {
                if (fputs("  cmp x9, x11\n  cset w9, lo\n"
                          "  and w9, w9, w15\n  orr w9, w9, w14\n", out) == EOF)
                    return false;
            }
            if (comparison) {
                *needs_gp_store = true;
                return true;
            }
            return fprintf(out, "  str x9, [sp, #%zu]\n"
                                "  str x10, [sp, #%zu]\n",
                           dst, dst + 8U) >= 0;
        }
    }
    if (ins->kind == MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO) {
        const MinicCoreValueId src = ins->value.operand;
        size_t from;
        if (src < fn->value_count &&
            minic_type_is_int128_integer(fn->values[src].type)) {
            if (!minic_lp64_frame_plan_wide_slot(frame_plan, src, &from) ||
                fprintf(out, "  ldr x9, [sp, #%zu]\n"
                             "  ldr x10, [sp, #%zu]\n"
                             "  orr x9, x9, x10\n"
                             "  cmp x9, #0\n  cset w9, eq\n",
                        from, from + 8U) < 0)
                return false;
            *needs_gp_store = true;
            return true;
        }
    }
    if (ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT &&
        minic_type_is_int128_integer(ins->type)) {
        const MinicCoreValueId lhs = ins->value.binary.left;
        const MinicCoreValueId rhs = ins->value.binary.right;
        size_t src, dst, rw;
        bool sign;
        if (lhs >= fn->value_count || rhs >= fn->value_count ||
            !minic_type_equal(fn->values[lhs].type, ins->type) ||
            !minic_lp64_scalar_integer_width(program, target,
                fn->values[rhs].type, &rw, &sign) ||
            !minic_lp64_frame_plan_wide_slot(frame_plan, lhs, &src) ||
            !minic_lp64_frame_plan_wide_slot(frame_plan, ins->result, &dst) ||
            fprintf(out, rw == 8U ? "  ldr x10, [sp, #%zu]\n"
                                   : "  ldr w10, [sp, #%zu]\n",
                    (size_t)rhs * 8U) < 0 ||
            fprintf(out, "  ldr x9, [sp, #%zu]\n"
                         "  ldr x11, [sp, #%zu]\n",
                    src, src + 8U) < 0 ||
            fputs("  cbz x10, 2f\n"
                  "  cmp x10, #64\n  b.hs 1f\n"
                  "  neg x13, x10\n"
                  "  lsr x13, x9, x13\n"
                  "  lsl x11, x11, x10\n"
                  "  orr x11, x11, x13\n"
                  "  lsl x9, x9, x10\n"
                  "  b 2f\n"
                  "1:\n"
                  "  sub x10, x10, #64\n"
                  "  lsl x11, x9, x10\n"
                  "  mov x9, xzr\n"
                  "2:\n", out) == EOF ||
            fprintf(out, "  str x9, [sp, #%zu]\n"
                         "  str x11, [sp, #%zu]\n",
                    dst, dst + 8U) < 0)
            return false;
        (void)sign;
        return true;
    }
    switch (ins->kind) {
        case MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW: {
            /* Complete 32/64-bit signed+unsigned GNU checked arithmetic.
             * Result bytes are written through Core's pointer; x9 carries
             * the boolean status to the normal SSA destination afterward. */
            const MinicCoreValueId lhs = ins->value.integer_overflow.left;
            const MinicCoreValueId rhs = ins->value.integer_overflow.right;
            const MinicCoreValueId address = ins->value.integer_overflow.result_address;
            size_t width;
            bool is_signed;
            if (!minic_core_integer_overflow_shape(
                    program, target, fn, ins, &width, &is_signed)) {
                bool multiply_signed_left, multiply_signed_right;
                bool multiply_signed_result;
                if (minic_core_integer_overflow_mixed_multiply_i32_shape(
                        program, target, fn, ins,
                        &multiply_signed_left, &multiply_signed_right,
                        &multiply_signed_result)) {
                    /* GNU checked arithmetic uses the mathematical product,
                     * not the result of C's usual integer promotions. The
                     * product of two 32-bit operands always fits 64 bits,
                     * even when one is signed and the other unsigned. */
                    if (fprintf(out, "  ldr w9, [sp, #%zu]\n"
                                     "  ldr w10, [sp, #%zu]\n"
                                     "  ldr x11, [sp, #%zu]\n",
                                (size_t)lhs * 8U, (size_t)rhs * 8U,
                                (size_t)address * 8U) < 0 ||
                        fputs(multiply_signed_left ? "  sxtw x9, w9\n"
                                                   : "  uxtw x9, w9\n", out) == EOF ||
                        fputs(multiply_signed_right ? "  sxtw x10, w10\n"
                                                    : "  uxtw x10, w10\n", out) == EOF ||
                        fputs("  mul x12, x9, x10\n"
                              "  mov w9, w12\n", out) == EOF ||
                        fputs(multiply_signed_result ? "  sxtw x13, w9\n"
                                                     : "  uxtw x13, w9\n", out) == EOF ||
                        fputs("  cmp x12, x13\n"
                              "  cset w10, ne\n"
                              "  str w9, [x11]\n"
                              "  mov w9, w10\n", out) == EOF)
                        return false;
                    *needs_gp_store = true;
                    return true;
                }
                bool signed_left;
                if (!minic_core_integer_overflow_mixed_add_i32_shape(
                        program, target, fn, ins, &signed_left) ||
                    fprintf(out, "  ldr x9, [sp, #%zu]\n"
                                 "  ldr x10, [sp, #%zu]\n"
                                 "  ldr x11, [sp, #%zu]\n",
                            (size_t)lhs * 8U, (size_t)rhs * 8U,
                            (size_t)address * 8U) < 0 ||
                    fputs(signed_left
                          ? "  sxtw x9, w9\n  asr x12, x9, #63\n"
                          : "  sxtw x10, w10\n  asr x12, x10, #63\n",
                          out) == EOF ||
                    fputs("  adds x9, x9, x10\n  adc x12, x12, xzr\n"
                          "  sxtw x13, w9\n  cmp x9, x13\n"
                          "  cset w14, ne\n  asr x13, x13, #63\n"
                          "  cmp x12, x13\n  cset w15, ne\n"
                          "  orr w10, w14, w15\n  str w9, [x11]\n"
                          "  mov w9, w10\n", out) == EOF)
                    return false;
                *needs_gp_store = true;
                return true;
            }
            const bool wide = width == 8U;
            const int op = ins->value.integer_overflow.operator_kind;
            if (fprintf(out, wide
                      ? "  ldr x9, [sp, #%zu]\n  ldr x10, [sp, #%zu]\n"
                      : "  ldr w9, [sp, #%zu]\n  ldr w10, [sp, #%zu]\n",
                        (size_t)lhs * 8U, (size_t)rhs * 8U) < 0 ||
                fprintf(out, "  ldr x11, [sp, #%zu]\n",
                        (size_t)address * 8U) < 0)
                return false;
            if (op == MINIC_CORE_INTEGER_OVERFLOW_ADD ||
                op == MINIC_CORE_INTEGER_OVERFLOW_SUBTRACT) {
                const bool sub = op == MINIC_CORE_INTEGER_OVERFLOW_SUBTRACT;
                const char *instruction = wide
                    ? (sub ? "  subs x9, x9, x10\n"
                           : "  adds x9, x9, x10\n")
                    : (sub ? "  subs w9, w9, w10\n"
                           : "  adds w9, w9, w10\n");
                const char *flag = is_signed ? "vs" : sub ? "cc" : "cs";
                if (fputs(instruction, out) == EOF ||
                    fprintf(out, "  cset w10, %s\n", flag) < 0)
                    return false;
            } else if (op == MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY) {
                if (wide) {
                    if (fputs(is_signed ? "  smulh x12, x9, x10\n"
                                        : "  umulh x12, x9, x10\n", out) == EOF ||
                        fputs("  mul x9, x9, x10\n", out) == EOF ||
                        fputs(is_signed
                             ? "  asr x10, x9, #63\n  cmp x12, x10\n"
                             : "  cmp x12, #0\n", out) == EOF)
                        return false;
                } else if (fputs(is_signed
                            ? "  smull x12, w9, w10\n"
                              "  sxtw x10, w12\n  cmp x12, x10\n"
                            : "  umull x12, w9, w10\n"
                              "  lsr x10, x12, #32\n  cmp x10, #0\n",
                            out) == EOF ||
                           fputs("  mov w9, w12\n", out) == EOF)
                    return false;
                if (fputs("  cset w10, ne\n", out) == EOF)
                    return false;
            } else return false;
            if (fputs(wide ? "  str x9, [x11]\n"
                          : "  str w9, [x11]\n", out) == EOF ||
                fputs("  mov w9, w10\n", out) == EOF)
                return false;
            *needs_gp_store = true;
            return true;
        }
        case MINIC_CORE_INSTRUCTION_INTEGER_CLZ:
        case MINIC_CORE_INSTRUCTION_INTEGER_CTZ: {
            const MinicCoreValueId operand = ins->value.operand;
            unsigned int width;
            if (!minic_core_integer_bitscan_shape(
                    program, target, fn, ins, &width) ||
                fprintf(out, width == 64U
                           ? "  ldr x9, [sp, #%zu]\n"
                           : "  ldr w9, [sp, #%zu]\n",
                        (size_t)operand * 8U) < 0 ||
                (width != 64U && width < 32U &&
                 fprintf(out, "  and w9, w9, #%u\n",
                         (1U << width) - 1U) < 0))
                return false;
            if (ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_CTZ &&
                fputs(width == 64U ? "  rbit x9, x9\n"
                                   : "  rbit w9, w9\n", out) == EOF)
                return false;
            if (fputs(width == 64U ? "  clz x9, x9\n"
                                   : "  clz w9, w9\n", out) == EOF ||
                (width < 32U &&
                 ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_CLZ &&
                 fprintf(out, "  sub w9, w9, #%u\n", 32U - width) < 0))
                return false;
            *needs_gp_store = true;
            return true;
        }
        case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT:
        case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT: {
            const MinicCoreValueId lhs = ins->value.binary.left;
            if (ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT &&
                minic_type_is_int128_integer(ins->type)) {
                const MinicCoreValueId rhs = ins->value.binary.right;
                size_t src_offset, dst_offset, rw;
                bool rs;
                const bool sign = minic_type_is_signed_integer(ins->type);
                if (lhs >= fn->value_count || rhs >= fn->value_count ||
                    !minic_type_is_int128_integer(fn->values[lhs].type) ||
                    !minic_lp64_scalar_integer_width(program, target,
                        fn->values[rhs].type, &rw, &rs) ||
                    !minic_lp64_frame_plan_wide_slot(frame_plan, lhs, &src_offset) ||
                    !minic_lp64_frame_plan_wide_slot(frame_plan, ins->result, &dst_offset) ||
                    fprintf(out, rw == 8U ?
                            "  ldr x10, [sp, #%zu]\n" :
                            "  ldr w10, [sp, #%zu]\n", (size_t)rhs * 8U) < 0 ||
                    fprintf(out, "  ldr x9, [sp, #%zu]\n"
                                 "  ldr x11, [sp, #%zu]\n",
                            src_offset, src_offset + 8U) < 0 ||
                    fputs("  cbz x10, 2f\n"
                          "  cmp x10, #64\n"
                          "  b.hs 1f\n"
                          "  neg x13, x10\n"
                          "  lsl x13, x11, x13\n"
                          "  lsr x9, x9, x10\n"
                          "  orr x9, x9, x13\n", out) == EOF ||
                    fputs(sign ? "  asr x11, x11, x10\n" :
                                 "  lsr x11, x11, x10\n", out) == EOF ||
                    fputs("  b 2f\n"
                          "1:\n"
                          "  sub x10, x10, #64\n", out) == EOF ||
                    fputs(sign ? "  asr x9, x11, x10\n"
                                 "  asr x11, x11, #63\n" :
                                 "  lsr x9, x11, x10\n"
                                 "  mov x11, xzr\n", out) == EOF ||
                    fprintf(out, "2:\n"
                                 "  str x9, [sp, #%zu]\n"
                                 "  str x11, [sp, #%zu]\n",
                            dst_offset, dst_offset + 8U) < 0)
                    return false;
                (void)rs;
                return true;
            }
            const MinicCoreValueId rhs = ins->value.binary.right;
            size_t lw, rw;
            bool ls, rs;
            bool wide;
            const char *op;
            if (!minic_multi_direct_integer_shift_shape(
                    program, target, fn, ins, &lw, &rw, &ls, &rs))
                return false;
            wide = lw == 8U;
            op = ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT
                     ? (wide ? "  lsl x9, x9, x10\n"
                             : "  lsl w9, w9, w10\n")
                     : (ls ? (wide ? "  asr x9, x9, x10\n"
                                   : "  asr w9, w9, w10\n")
                           : (wide ? "  lsr x9, x9, x10\n"
                                   : "  lsr w9, w9, w10\n"));
            if (fprintf(out, wide ? "  ldr x9, [sp, #%zu]\n"
                                  : "  ldr w9, [sp, #%zu]\n",
                        (size_t)lhs * 8U) < 0 ||
                /* Narrow signed shifts need sign-extension BEFORE the
                 * arithmetic shift; normalizing only the result is late. */
                (lw < 4U && !minic_aarch64_emit_integer_normalize(out, lw, ls)) ||
                fprintf(out, rw == 8U ? "  ldr x10, [sp, #%zu]\n"
                                     : "  ldr w10, [sp, #%zu]\n",
                        (size_t)rhs * 8U) < 0 ||
                fputs(op, out) == EOF ||
                (!wide && !minic_aarch64_emit_integer_normalize(out, lw, ls)))
                return false;
            (void)rs;
            *needs_gp_store = true;
            return true;
        }
        /* Original cJSON uses ~ on signed/unsigned scalar values.
         * Preserve C-width truncation for narrow integers and the full
         * 64-bit complement for LP64 values. */
        case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT: {
            const MinicCoreValueId operand = ins->value.operand;
            size_t width;
            bool sign;
            if (!minic_multi_direct_integer_unary_shape(
                    program, target, fn, ins, &width, &sign) ||
                fprintf(out, width == 8U ? "  ldr x9, [sp, #%zu]\n"
                                         : "  ldr w9, [sp, #%zu]\n",
                        (size_t)operand * 8U) < 0 ||
                fputs(width == 8U ? "  mvn x9, x9\n" : "  mvn w9, w9\n",
                      out) == EOF ||
                (width != 8U && !minic_aarch64_emit_integer_normalize(out, width, sign)))
                return false;
            *needs_gp_store = true;
            return true;
        }
        case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE: {
            const MinicCoreValueId operand = ins->value.operand;
            size_t width;
            bool sign;
            if (!minic_multi_direct_integer_unary_shape(
                    program, target, fn, ins, &width, &sign) ||
                fprintf(out, width == 8U ? "  ldr x9, [sp, #%zu]\n"
                                         : "  ldr w9, [sp, #%zu]\n",
                        (size_t)operand * 8U) < 0 ||
                fputs(width == 8U ? "  neg x9, x9\n" : "  neg w9, w9\n",
                      out) == EOF ||
                (width != 8U && !minic_aarch64_emit_integer_normalize(out, width, sign)))
                return false;
            *needs_gp_store = true;
            return true;
        }
        case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
        case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
        case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY: {
            MinicCoreValueId lhs = ins->value.binary.left;
            MinicCoreValueId rhs = ins->value.binary.right;
            size_t lw, rw;
            bool ls, rs;
            bool wide;
            const char *opcode;
            if (!minic_multi_direct_integer_binary_shape(
                    program, target, fn, ins, &lw, &rw, &ls, &rs))
                return false;
            wide = lw == 8U;
            /* M36: target-native LP64 multiplication, including cJSON's
             * size_t newsize = needed * 2; low 64 bits follow C unsigned
             * arithmetic; signed overflow is undefined at the source. */
            opcode = ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_ADD
                ? (wide ? "  add x9, x9, x10\n" : "  add w9, w9, w10\n")
                : ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT
                    ? (wide ? "  sub x9, x9, x10\n" : "  sub w9, w9, w10\n")
                    : (wide ? "  mul x9, x9, x10\n" : "  mul w9, w9, w10\n");
            if (fprintf(out, wide ? "  ldr x9, [sp, #%zu]\n"
                                  : "  ldr w9, [sp, #%zu]\n",
                        (size_t)lhs * 8U) < 0 ||
                fprintf(out, wide ? "  ldr x10, [sp, #%zu]\n"
                                  : "  ldr w10, [sp, #%zu]\n",
                        (size_t)rhs * 8U) < 0 ||
                fputs(opcode, out) == EOF ||
                (!wide && !minic_aarch64_emit_integer_normalize(out, lw, ls)))
                return false;
            *needs_gp_store = true;
            return true;
        }
        case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:
        case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:
        case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR: {
            const MinicCoreValueId lhs = ins->value.binary.left;
            const MinicCoreValueId rhs = ins->value.binary.right;
            size_t lw, rw;
            bool ls, rs;
            bool wide;
            const char *opcode;
            if (!minic_multi_direct_integer_binary_shape(
                    program, target, fn, ins, &lw, &rw, &ls, &rs))
                return false;
            wide = lw == 8U;
            opcode = ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND
                ? (wide ? "  and x9, x9, x10\n" : "  and w9, w9, w10\n")
                : ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR
                    ? (wide ? "  eor x9, x9, x10\n" : "  eor w9, w9, w10\n")
                    : (wide ? "  orr x9, x9, x10\n" : "  orr w9, w9, w10\n");
            if (fprintf(out, wide ? "  ldr x9, [sp, #%zu]\n"
                                  : "  ldr w9, [sp, #%zu]\n",
                        (size_t)lhs * 8U) < 0 ||
                fprintf(out, wide ? "  ldr x10, [sp, #%zu]\n"
                                  : "  ldr w10, [sp, #%zu]\n",
                        (size_t)rhs * 8U) < 0 ||
                fputs(opcode, out) == EOF ||
                (lw != 8U && !minic_aarch64_emit_integer_normalize(out, lw, ls)))
                return false;
            *needs_gp_store = true;
            return true;
        }
        case MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE:
        case MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER: {
            const MinicCoreValueId lhs = ins->value.binary.left;
            const MinicCoreValueId rhs = ins->value.binary.right;
            size_t lw, rw;
            bool ls, rs, wide;
            const char *op;
            if (!minic_multi_direct_integer_binary_shape(
                    program, target, fn, ins, &lw, &rw, &ls, &rs))
                return false;
            wide = lw == 8U;
            if (fprintf(out, wide ? "  ldr x9, [sp, #%zu]\n"
                                  : "  ldr w9, [sp, #%zu]\n",
                        (size_t)lhs * 8U) < 0 ||
                fprintf(out, wide ? "  ldr x10, [sp, #%zu]\n"
                                  : "  ldr w10, [sp, #%zu]\n",
                        (size_t)rhs * 8U) < 0)
                return false;
            op = ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE
                ? (wide ? (ls ? "  sdiv x9, x9, x10\n"
                               : "  udiv x9, x9, x10\n")
                        : (ls ? "  sdiv w9, w9, w10\n"
                               : "  udiv w9, w9, w10\n"))
                : (wide ? (ls ? "  sdiv x11, x9, x10\n"
                               "  msub x9, x11, x10, x9\n"
                             : "  udiv x11, x9, x10\n"
                               "  msub x9, x11, x10, x9\n")
                        : (ls ? "  sdiv w11, w9, w10\n"
                               "  msub w9, w11, w10, w9\n"
                             : "  udiv w11, w9, w10\n"
                               "  msub w9, w11, w10, w9\n"));
            if (fputs(op, out) == EOF)
                return false;
            *needs_gp_store = true;
            return true;
        }
        /* M33: cJSON_SetValuestring uses pointer ordering for overlap
         * checks; the Core verifier has confirmed compatible pointer types.
         * Compare the entire 64-bit address unsigned (not low 32 bits). */
        case MINIC_CORE_INSTRUCTION_POINTER_LESS: {
            const MinicCoreValueId lhs = ins->value.binary.left;
            const MinicCoreValueId rhs = ins->value.binary.right;
            if (!minic_multi_direct_pointer_less_shape(fn, ins) ||
                fprintf(out, "  ldr x9, [sp, #%zu]\n"
                             "  ldr x10, [sp, #%zu]\n"
                             "  cmp x9, x10\n"
                             "  cset w9, lo\n",
                        (size_t)lhs * 8U, (size_t)rhs * 8U) < 0)
                return false;
            *needs_gp_store = true;
            return true;
        }
        case MINIC_CORE_INSTRUCTION_INTEGER_LESS:
        case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL: {
            const MinicCoreValueId lhs = ins->value.binary.left;
            const MinicCoreValueId rhs = ins->value.binary.right;
            size_t lw, rw;
            bool ls, rs, wide;
            if (ins->kind == MINIC_CORE_INSTRUCTION_SCALAR_EQUAL &&
                lhs < fn->value_count && rhs < fn->value_count &&
                minic_type_is_pointer(fn->values[lhs].type) &&
                minic_type_is_pointer(fn->values[rhs].type)) {
                if (fprintf(out, "  ldr x9, [sp, #%zu]\n"
                                 "  ldr x10, [sp, #%zu]\n"
                                 "  cmp x9, x10\n  cset w9, eq\n",
                            (size_t)lhs * 8U, (size_t)rhs * 8U) < 0)
                    return false;
                *needs_gp_store = true;
            return true;
            }
            if (!minic_multi_direct_integer_compare_shape(
                    program, target, fn, ins, &lw, &rw, &ls, &rs))
                return false;
            wide = lw == 8U;
            if (fprintf(out, wide ? "  ldr x9, [sp, #%zu]\n"
                                  : "  ldr w9, [sp, #%zu]\n",
                        (size_t)lhs * 8U) < 0 ||
                fprintf(out, wide ? "  ldr x10, [sp, #%zu]\n"
                                  : "  ldr w10, [sp, #%zu]\n",
                        (size_t)rhs * 8U) < 0 ||
                fputs(ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_LESS
                          ? (wide
                              ? (ls ? "  cmp x9, x10\n  cset w9, lt\n"
                                    : "  cmp x9, x10\n  cset w9, lo\n")
                              : (ls ? "  cmp w9, w10\n  cset w9, lt\n"
                                    : "  cmp w9, w10\n  cset w9, lo\n"))
                          : (wide ? "  cmp x9, x10\n  cset w9, eq\n"
                                  : "  cmp w9, w10\n  cset w9, eq\n"), out) == EOF)
                return false;
            *needs_gp_store = true;
            return true;
        }
        case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO: {
            const MinicCoreValueId v = ins->value.operand;
            size_t width = 0U;
            bool sign = false;
            bool pointer, wide;
            if (!minic_multi_direct_scalar_zero_shape(
                    program, target, fn, ins, &width, &sign, &pointer))
                return false;
            wide = pointer || width == 8U;
            if (fprintf(out, wide ? "  ldr x9, [sp, #%zu]\n"
                                  : "  ldr w9, [sp, #%zu]\n",
                        (size_t)v * 8U) < 0 ||
                fputs(wide ? "  cmp x9, #0\n  cset w9, eq\n"
                           : "  cmp w9, #0\n  cset w9, eq\n", out) == EOF)
                return false;
            (void)sign;
            *needs_gp_store = true;
            return true;
        }
        default:
            return false;
    }
}
