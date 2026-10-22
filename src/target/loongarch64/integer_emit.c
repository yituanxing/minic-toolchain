#include "target/loongarch64/integer_emit.h"
#include "target/loongarch64/scalar_normalize.h"
#include "target/common/direct_instruction_semantics.h"
#include "target/common/lp64_scalar.h"
#include "target/loongarch64/frame_emit.h"
#include <stdio.h>

/* Keep dynamic-stack addressing behind the target frame helper. */
#define emit_sp_mem(_out, _op, _reg, _offset) \
    minic_loongarch64_emit_frame_mem((_out), (_op), (_reg), (_offset), dynamic_frame)

/* Core integer/bit/pointer ALU instruction family. Operand shape/width
 * admission is shared; native opcodes, overflow flags and pair shifts
 * are target-owned. Scalar results use the caller's GP SSA writeback,
 * while 128-bit shifts directly write their two frame-slot halves. */
bool minic_loongarch64_emit_integer_instruction(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    const MinicLP64FramePlan *frame_plan,
    bool dynamic_frame,
    bool *needs_gp_store) {
    if (out == NULL || program == NULL || target == NULL ||
        fn == NULL || ins == NULL || frame_plan == NULL ||
         needs_gp_store == NULL)
        return false;
    *needs_gp_store = false;
    switch (ins->kind) {
        case MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW: {
            /* Signed overflow is computed from the sign bits; unsigned
             * carry/borrow use full-width comparisons. 64-bit multiply
             * checks MULH high against the low-half sign extension. */
            const MinicCoreValueId lhs = ins->value.integer_overflow.left;
            const MinicCoreValueId rhs = ins->value.integer_overflow.right;
            const MinicCoreValueId address = ins->value.integer_overflow.result_address;
            size_t width;
            bool is_signed;
            if (!minic_multi_direct_integer_overflow_shape(
                    program, target, fn, ins, &width, &is_signed))
                return false;
            const int op = ins->value.integer_overflow.operator_kind;
            const bool wide = width == 8U;
            if (!emit_sp_mem(out, wide ? "ld.d" : "ld.w",
                             "$t0", (size_t)lhs * 8U) ||
                !emit_sp_mem(out, wide ? "ld.d" : "ld.w",
                             "$t1", (size_t)rhs * 8U) ||
                !emit_sp_mem(out, "ld.d",
                             "$t2", (size_t)address * 8U))
                return false;
            if (!wide && !is_signed &&
                fputs("  bstrpick.d $t0, $t0, 31, 0\n"
                      "  bstrpick.d $t1, $t1, 31, 0\n", out) == EOF)
                return false;
            if (op == MINIC_CORE_INTEGER_OVERFLOW_ADD ||
                op == MINIC_CORE_INTEGER_OVERFLOW_SUBTRACT) {
                const bool sub = op == MINIC_CORE_INTEGER_OVERFLOW_SUBTRACT;
                if (fputs("  or $t3, $t0, $zero\n", out) == EOF ||
                    fputs(wide
                          ? (sub ? "  sub.d $t0, $t0, $t1\n"
                                 : "  add.d $t0, $t0, $t1\n")
                          : (!is_signed && !sub
                              ? "  add.d $t0, $t0, $t1\n"
                              : sub ? "  sub.w $t0, $t0, $t1\n"
                                    : "  add.w $t0, $t0, $t1\n"), out) == EOF)
                    return false;
                if (is_signed) {
                    if (fputs(sub
                          ? "  xor $t4, $t3, $t1\n"
                            "  xor $t3, $t3, $t0\n"
                          : "  xor $t4, $t1, $t0\n"
                            "  xor $t3, $t3, $t0\n", out) == EOF ||
                        fputs("  and $t4, $t3, $t4\n", out) == EOF ||
                        (!wide && fputs("  slli.w $t4, $t4, 0\n", out) == EOF) ||
                        fputs("  slt $t4, $t4, $zero\n", out) == EOF)
                        return false;
                } else if (fputs(sub ? "  sltu $t4, $t3, $t1\n"
                                     : wide ? "  sltu $t4, $t0, $t3\n"
                                            : "  srli.d $t4, $t0, 32\n",
                                 out) == EOF)
                    return false;
            } else if (op == MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY) {
                if (wide) {
                    if (fputs(is_signed ? "  mulh.d $t3, $t0, $t1\n"
                                        : "  mulh.du $t3, $t0, $t1\n", out) == EOF ||
                        fputs("  mul.d $t0, $t0, $t1\n", out) == EOF ||
                        (is_signed &&
                         fputs("  srai.d $t4, $t0, 63\n"
                               "  xor $t3, $t3, $t4\n", out) == EOF) ||
                        fputs("  sltu $t4, $zero, $t3\n", out) == EOF)
                        return false;
                } else if (fputs("  mul.d $t3, $t0, $t1\n"
                                 "  add.w $t0, $t3, $zero\n", out) == EOF ||
                           (is_signed
                            ? fputs("  xor $t4, $t3, $t0\n"
                                    "  sltu $t4, $zero, $t4\n", out) == EOF
                            : fputs("  srli.d $t4, $t3, 32\n"
                                    "  sltu $t4, $zero, $t4\n", out) == EOF))
                    return false;
            } else return false;
            if (fputs(wide ? "  st.d $t0, $t2, 0\n"
                          : "  st.w $t0, $t2, 0\n", out) == EOF ||
                fputs("  or $t0, $t4, $zero\n", out) == EOF)
                return false;
            *needs_gp_store = true;
            return true;
        }
        case MINIC_CORE_INSTRUCTION_INTEGER_CLZ:
        case MINIC_CORE_INSTRUCTION_INTEGER_CTZ: {
            const MinicCoreValueId operand = ins->value.operand;
            unsigned int width;
            if (!minic_multi_direct_integer_bitscan_shape(
                    program, target, fn, ins, &width) ||
                !emit_sp_mem(out, width == 64U ? "ld.d" : "ld.w",
                             "$t0", (size_t)operand * 8U))
                return false;
            if (width < 32U &&
                fputs(width == 8U
                          ? "  bstrpick.w $t0, $t0, 7, 0\n"
                          : "  bstrpick.w $t0, $t0, 15, 0\n", out) == EOF)
                return false;
            if (fputs(ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_CLZ
                      ? (width == 64U ? "  clz.d $t0, $t0\n"
                                       : "  clz.w $t0, $t0\n")
                      : (width == 64U ? "  ctz.d $t0, $t0\n"
                                       : "  ctz.w $t0, $t0\n"), out) == EOF ||
                (width < 32U &&
                 ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_CLZ &&
                 fprintf(out, "  addi.w $t0, $t0, -%u\n",
                         32U - width) < 0))
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
                    !emit_sp_mem(out, "ld.d", "$t0", src_offset) ||
                    !emit_sp_mem(out, "ld.d", "$t1", src_offset + 8U) ||
                    !emit_sp_mem(out, rw == 8U ? "ld.d" : "ld.w",
                                 "$t2", (size_t)rhs * 8U) ||
                    fputs("  beqz $t2, 2f\n"
                          "  li.d $t4, 64\n"
                          "  bgeu $t2, $t4, 1f\n"
                          "  sub.d $t4, $zero, $t2\n"
                          "  sll.d $t4, $t1, $t4\n"
                          "  srl.d $t0, $t0, $t2\n"
                          "  or $t0, $t0, $t4\n", out) == EOF ||
                    fputs(sign ? "  sra.d $t1, $t1, $t2\n" :
                                 "  srl.d $t1, $t1, $t2\n", out) == EOF ||
                    fputs("  b 2f\n"
                          "1:\n"
                          "  addi.d $t2, $t2, -64\n", out) == EOF ||
                    fputs(sign ? "  sra.d $t0, $t1, $t2\n"
                                 "  srai.d $t1, $t1, 63\n" :
                                 "  srl.d $t0, $t1, $t2\n"
                                 "  move $t1, $zero\n", out) == EOF ||
                    fputs("2:\n", out) == EOF ||
                    !emit_sp_mem(out, "st.d", "$t0", dst_offset) ||
                    !emit_sp_mem(out, "st.d", "$t1", dst_offset + 8U))
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
                    ? (wide ? "  sll.d $t0, $t0, $t1\n"
                            : "  sll.w $t0, $t0, $t1\n")
                    : (ls ? (wide ? "  sra.d $t0, $t0, $t1\n"
                                  : "  sra.w $t0, $t0, $t1\n")
                          : (wide ? "  srl.d $t0, $t0, $t1\n"
                                  : "  srl.w $t0, $t0, $t1\n"));
            if (!emit_sp_mem(out, wide ? "ld.d" : "ld.w",
                             "$t0", (size_t)lhs * 8U) ||
                /* Signed narrow shift inputs require extension before sra. */
                (lw < 4U && !minic_loongarch64_emit_integer_normalize(out, lw, ls)) ||
                !emit_sp_mem(out, rw == 8U ? "ld.d" : "ld.w",
                             "$t1", (size_t)rhs * 8U) ||
                fputs(op, out) == EOF ||
                (!wide && !minic_loongarch64_emit_integer_normalize(out, lw, ls)))
                return false;
            (void)rs;
            *needs_gp_store = true;
            return true;
        }
        /* Original cJSON uses ~ on signed/unsigned scalar values.
         * NOR against zero is the LoongArch native complement; normalize
         * subword results before storing the Core SSA slot. */
        case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT: {
            const MinicCoreValueId operand = ins->value.operand;
            size_t width;
            bool sign;
            if (!minic_multi_direct_integer_unary_shape(
                    program, target, fn, ins, &width, &sign) ||
                !emit_sp_mem(out, width == 8U ? "ld.d" : "ld.w",
                             "$t0", (size_t)operand * 8U) ||
                fputs("  nor $t0, $t0, $zero\n", out) == EOF ||
                (width != 8U && !minic_loongarch64_emit_integer_normalize(out, width, sign)))
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
                !emit_sp_mem(out, width == 8U ? "ld.d" : "ld.w",
                             "$t0", (size_t)operand * 8U) ||
                fputs(width == 8U ? "  sub.d $t0, $zero, $t0\n"
                                  : "  sub.w $t0, $zero, $t0\n", out) == EOF ||
                (width != 8U && !minic_loongarch64_emit_integer_normalize(out, width, sign)))
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
            /* M36: real LP64 multiplication uses the full 64-bit operands. */
            opcode = ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_ADD
                ? (wide ? "  add.d $t0, $t0, $t1\n" : "  add.w $t0, $t0, $t1\n")
                : ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT
                    ? (wide ? "  sub.d $t0, $t0, $t1\n" : "  sub.w $t0, $t0, $t1\n")
                    : (wide ? "  mul.d $t0, $t0, $t1\n" : "  mul.w $t0, $t0, $t1\n");
            if (!emit_sp_mem(out, wide ? "ld.d" : "ld.w", "$t0",
                             (size_t)lhs * 8U) ||
                !emit_sp_mem(out, wide ? "ld.d" : "ld.w", "$t1",
                             (size_t)rhs * 8U) ||
                fputs(opcode, out) == EOF ||
                (!wide && !minic_loongarch64_emit_integer_normalize(out, lw, ls)))
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
            const char *opcode;
            if (!minic_multi_direct_integer_binary_shape(
                    program, target, fn, ins, &lw, &rw, &ls, &rs))
                return false;
            opcode = ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND
                ? "  and $t0, $t0, $t1\n"
                : ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR
                    ? "  xor $t0, $t0, $t1\n"
                    : "  or $t0, $t0, $t1\n";
            if (!emit_sp_mem(out, lw == 8U ? "ld.d" : "ld.w",
                             "$t0", (size_t)lhs * 8U) ||
                !emit_sp_mem(out, rw == 8U ? "ld.d" : "ld.w",
                             "$t1", (size_t)rhs * 8U) ||
                fputs(opcode, out) == EOF ||
                (lw != 8U && !minic_loongarch64_emit_integer_normalize(out, lw, ls)))
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
            op = ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE
                ? (wide ? (ls ? "  div.d $t0, $t0, $t1\n"
                               : "  div.du $t0, $t0, $t1\n")
                        : (ls ? "  div.w $t0, $t0, $t1\n"
                               : "  div.wu $t0, $t0, $t1\n"))
                : (wide ? (ls ? "  mod.d $t0, $t0, $t1\n"
                               : "  mod.du $t0, $t0, $t1\n")
                        : (ls ? "  mod.w $t0, $t0, $t1\n"
                               : "  mod.wu $t0, $t0, $t1\n"));
            if (!emit_sp_mem(out, wide ? "ld.d" : "ld.w",
                             "$t0", (size_t)lhs * 8U) ||
                !emit_sp_mem(out, wide ? "ld.d" : "ld.w",
                             "$t1", (size_t)rhs * 8U) ||
                fputs(op, out) == EOF)
                return false;
            *needs_gp_store = true;
            return true;
        }
        /* M33: unsigned 64-bit pointer order for cJSON overlap checks.
         * Operand pointer equivalence is also enforced in Core IR. */
        case MINIC_CORE_INSTRUCTION_POINTER_LESS: {
            const MinicCoreValueId lhs = ins->value.binary.left;
            const MinicCoreValueId rhs = ins->value.binary.right;
            if (!minic_multi_direct_pointer_less_shape(fn, ins) ||
                !emit_sp_mem(out, "ld.d", "$t0", (size_t)lhs * 8U) ||
                !emit_sp_mem(out, "ld.d", "$t1", (size_t)rhs * 8U) ||
                fputs("  sltu $t0, $t0, $t1\n", out) == EOF)
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
                if (!emit_sp_mem(out, "ld.d", "$t0", (size_t)lhs * 8U) ||
                    !emit_sp_mem(out, "ld.d", "$t1", (size_t)rhs * 8U) ||
                    fputs("  xor $t0, $t0, $t1\n"
                          "  sltui $t0, $t0, 1\n", out) == EOF)
                    return false;
                *needs_gp_store = true;
            return true;
            }
            if (!minic_multi_direct_integer_compare_shape(
                    program, target, fn, ins, &lw, &rw, &ls, &rs))
                return false;
            wide = lw == 8U;
            if (!emit_sp_mem(out, wide ? "ld.d" : "ld.w", "$t0",
                             (size_t)lhs * 8U) ||
                !emit_sp_mem(out, wide ? "ld.d" : "ld.w", "$t1",
                             (size_t)rhs * 8U) ||
                fputs(ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_LESS
                          ? (ls ? "  slt $t0, $t0, $t1\n"
                                : "  sltu $t0, $t0, $t1\n")
                          : "  xor $t0, $t0, $t1\n  sltui $t0, $t0, 1\n", out) == EOF)
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
            if (!emit_sp_mem(out, wide ? "ld.d" : "ld.w", "$t0",
                             (size_t)v * 8U) ||
                fputs("  sltui $t0, $t0, 1\n", out) == EOF)
                return false;
            (void)sign;
            *needs_gp_store = true;
            return true;
        }
        default:
            return false;
    }
}

#undef emit_sp_mem
