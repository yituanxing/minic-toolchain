#include "target/loongarch64/integer_emit.h"
#include "target/loongarch64/scalar_normalize.h"
#include "target/common/direct_instruction_semantics.h"
#include "target/common/core_numeric_shape.h"
#include "target/common/lp64_scalar.h"
#include "target/loongarch64/frame_emit.h"
#include <inttypes.h>
#include <stdio.h>

/* Core SSA constants are immutable and retain their defining instruction.
 * An immediate machine operand may read those bits directly without a
 * redundant frame reload.  Do not infer constants from arbitrary local
 * slots, which can be rewritten by volatile/ASM or aliasing effects. */
static bool loong_integer_constant_rhs(const MinicCoreFunction *fn,
    MinicCoreValueId id, int64_t *value) {
    const MinicCoreInstruction *definition;
    if (fn == NULL || value == NULL || id >= fn->value_count ||
        fn->values[id].definition >= fn->instruction_count)
        return false;
    definition = &fn->instructions[fn->values[id].definition];
    if (definition->result != id ||
        definition->kind != MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT)
        return false;
    *value = definition->value.integer_value;
    return true;
}

/* Keep machine-immediate proof in one ISA owner. The dispatcher can then
 * remove a single-use constant materialization whose only consumer reads
 * its immutable Core definition directly; the emitter uses the identical
 * encoding range rather than independently guessing at valid immediates. */
bool minic_loongarch64_integer_rhs_immediate(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    MinicCoreValueId *rhs_id, int64_t *encoded_immediate) {
    MinicCoreValueId rhs;
    size_t lw, rw;
    bool ls, rs;
    int64_t constant;
    if (program == NULL || target == NULL || fn == NULL || ins == NULL)
        return false;
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
    case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:
        break;
    default:
        return false;
    }
    if (!minic_multi_direct_integer_binary_shape(
            program, target, fn, ins, &lw, &rw, &ls, &rs))
        return false;
    rhs = ins->value.binary.right;
    if (!loong_integer_constant_rhs(fn, rhs, &constant))
        return false;
    if (ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_ADD) {
        if (constant < -2048 || constant > 2047) return false;
    } else if (ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT) {
        if (constant < -2047 || constant > 2048) return false;
        constant = -constant;
    } else if (constant < 0 || constant > 4095) {
        return false;
    }
    if (rhs_id != NULL) *rhs_id = rhs;
    if (encoded_immediate != NULL) *encoded_immediate = constant;
    return true;
}

/* GNU checked-addition uses the original *mathematical* operand values,
 * then converts to the destination type. Linux fs/udf/inode.c passes a
 * compile-time sizeof that Core legally narrows to a signed i32 constant,
 * and le32_to_cpu() supplies u32. The i32+u32 sum fits signed i64, while
 * the signed i32 destination can overflow. Keep this contract explicit:
 * never treat the u32 as signed or infer overflow from add.w alone. */
static bool loong_checked_add_i32_u32_shape(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    bool *signed_left) {
    MinicCoreValueId left, right, address;
    MinicType result;
    if (fn == NULL || ins == NULL || signed_left == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW ||
        ins->value.integer_overflow.operator_kind !=
            MINIC_CORE_INTEGER_OVERFLOW_ADD)
        return false;
    left = ins->value.integer_overflow.left;
    right = ins->value.integer_overflow.right;
    address = ins->value.integer_overflow.result_address;
    if (left >= fn->value_count || right >= fn->value_count ||
        address >= fn->value_count ||
        !minic_type_pointee(fn->values[address].type, &result) ||
        !minic_type_equal(result, minic_type_int()))
        return false;
    if (minic_type_equal(fn->values[left].type, minic_type_int()) &&
        minic_type_equal(fn->values[right].type,
                         minic_type_unsigned_int())) {
        *signed_left = true;
        return true;
    }
    if (minic_type_equal(fn->values[left].type,
                         minic_type_unsigned_int()) &&
        minic_type_equal(fn->values[right].type, minic_type_int())) {
        *signed_left = false;
        return true;
    }
    return false;
}

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
    bool dynamic_frame, bool lhs_in_previous_gp,
    bool *needs_gp_store) {
    if (out == NULL || program == NULL || target == NULL ||
        fn == NULL || ins == NULL || frame_plan == NULL ||
         needs_gp_store == NULL)
        return false;
    *needs_gp_store = false;
    /* Two 64-bit limbs per Core TI value, with explicit carry/borrow.
     * The LP64D ABI transfer remains owned by call_emit.c. */
    if ((ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_NEGATE ||
         ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT) &&
        minic_type_is_int128_integer(ins->type)) {
        const MinicCoreValueId src = ins->value.operand;
        size_t from, to;
        if (src >= fn->value_count ||
            !minic_type_equal(fn->values[src].type, ins->type) ||
            !minic_lp64_frame_plan_wide_slot(frame_plan, src, &from) ||
            !minic_lp64_frame_plan_wide_slot(frame_plan, ins->result, &to) ||
            !emit_sp_mem(out, "ld.d", "$t0", from) ||
            !emit_sp_mem(out, "ld.d", "$t1", from + 8U) ||
            fputs(ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_NEGATE
                  ? "  sub.d $t3, $zero, $t0\n"
                    "  sltu $t4, $zero, $t0\n"
                    "  sub.d $t1, $zero, $t1\n"
                    "  sub.d $t1, $t1, $t4\n"
                    "  move $t0, $t3\n"
                  : "  nor $t0, $t0, $zero\n"
                    "  nor $t1, $t1, $zero\n", out) == EOF ||
            !emit_sp_mem(out, "st.d", "$t0", to) ||
            !emit_sp_mem(out, "st.d", "$t1", to + 8U))
            return false;
        return true;
    }
    if (ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_ADD ||
        ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT ||
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
                !emit_sp_mem(out, "ld.d", "$t0", left) ||
                !emit_sp_mem(out, "ld.d", "$t1", left + 8U) ||
                !emit_sp_mem(out, "ld.d", "$t2", right) ||
                !emit_sp_mem(out, "ld.d", "$t3", right + 8U))
                return false;
            switch (ins->kind) {
            case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
                op = "  move $t4, $t0\n"
                     "  add.d $t0, $t0, $t2\n"
                     "  sltu $t4, $t0, $t4\n"
                     "  add.d $t1, $t1, $t3\n"
                     "  add.d $t1, $t1, $t4\n";
                break;
            case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
                op = "  sltu $t4, $t0, $t2\n"
                     "  sub.d $t0, $t0, $t2\n"
                     "  sub.d $t1, $t1, $t3\n"
                     "  sub.d $t1, $t1, $t4\n";
                break;
            case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:
                op = "  and $t0, $t0, $t2\n  and $t1, $t1, $t3\n";
                break;
            case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:
                op = "  xor $t0, $t0, $t2\n  xor $t1, $t1, $t3\n";
                break;
            case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:
                op = "  or $t0, $t0, $t2\n  or $t1, $t1, $t3\n";
                break;
            case MINIC_CORE_INSTRUCTION_INTEGER_LESS:
                op = sign
                    ? "  xor $t4, $t1, $t3\n"
                      "  sltui $t4, $t4, 1\n"
                      "  slt $t1, $t1, $t3\n"
                    : "  xor $t4, $t1, $t3\n"
                      "  sltui $t4, $t4, 1\n"
                      "  sltu $t1, $t1, $t3\n";
                break;
            case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:
                op = "  xor $t0, $t0, $t2\n"
                     "  xor $t1, $t1, $t3\n"
                     "  or $t0, $t0, $t1\n"
                     "  sltui $t0, $t0, 1\n";
                break;
            default: return false;
            }
            if (fputs(op, out) == EOF) return false;
            if (ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_LESS &&
                fputs("  sltu $t0, $t0, $t2\n"
                      "  and $t0, $t0, $t4\n"
                      "  or $t0, $t0, $t1\n", out) == EOF)
                return false;
            if (comparison) {
                *needs_gp_store = true;
                return true;
            }
            return emit_sp_mem(out, "st.d", "$t0", dst) &&
                   emit_sp_mem(out, "st.d", "$t1", dst + 8U);
        }
    }
    if (ins->kind == MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO) {
        const MinicCoreValueId src = ins->value.operand;
        size_t from;
        if (src < fn->value_count &&
            minic_type_is_int128_integer(fn->values[src].type)) {
            if (!minic_lp64_frame_plan_wide_slot(frame_plan, src, &from) ||
                !emit_sp_mem(out, "ld.d", "$t0", from) ||
                !emit_sp_mem(out, "ld.d", "$t1", from + 8U) ||
                fputs("  or $t0, $t0, $t1\n"
                      "  sltui $t0, $t0, 1\n", out) == EOF)
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
            !emit_sp_mem(out, "ld.d", "$t0", src) ||
            !emit_sp_mem(out, "ld.d", "$t1", src + 8U) ||
            !emit_sp_mem(out, rw == 8U ? "ld.d" : "ld.w", "$t2",
                         minic_lp64_frame_value_offset(frame_plan, rhs)) ||
            fputs("  beqz $t2, 2f\n"
                  "  li.d $t4, 64\n"
                  "  bgeu $t2, $t4, 1f\n"
                  "  sub.d $t3, $zero, $t2\n"
                  "  srl.d $t3, $t0, $t3\n"
                  "  sll.d $t1, $t1, $t2\n"
                  "  or $t1, $t1, $t3\n"
                  "  sll.d $t0, $t0, $t2\n"
                  "  b 2f\n"
                  "1:\n"
                  "  addi.d $t2, $t2, -64\n"
                  "  sll.d $t1, $t0, $t2\n"
                  "  move $t0, $zero\n"
                  "2:\n", out) == EOF ||
            !emit_sp_mem(out, "st.d", "$t0", dst) ||
            !emit_sp_mem(out, "st.d", "$t1", dst + 8U))
            return false;
        (void)sign;
        return true;
    }
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
            if (!minic_core_integer_overflow_shape(
                    program, target, fn, ins, &width, &is_signed)) {
                bool multiply_signed_left, multiply_signed_right;
                bool multiply_signed_result;
                bool add_signed_left;
                if (loong_checked_add_i32_u32_shape(fn, ins,
                                                    &add_signed_left)) {
                    if (!emit_sp_mem(out, "ld.w", "$t0",
                                     minic_lp64_frame_value_offset(frame_plan, lhs)) ||
                        !emit_sp_mem(out, "ld.w", "$t1",
                                     minic_lp64_frame_value_offset(frame_plan, rhs)) ||
                        !emit_sp_mem(out, "ld.d", "$t2",
                                     minic_lp64_frame_value_offset(frame_plan, address)) ||
                        fputs(add_signed_left
                                  ? "  bstrpick.d $t1, $t1, 31, 0\n"
                                  : "  bstrpick.d $t0, $t0, 31, 0\n",
                              out) == EOF ||
                        fputs("  add.d $t3, $t0, $t1\n"
                              "  add.w $t0, $t3, $zero\n"
                              "  xor $t4, $t3, $t0\n"
                              "  sltu $t4, $zero, $t4\n"
                              "  st.w $t0, $t2, 0\n"
                              "  move $t0, $t4\n", out) == EOF)
                        return false;
                    *needs_gp_store = true;
                    return true;
                }
                if (minic_core_integer_overflow_mixed_multiply_i32_shape(
                        program, target, fn, ins,
                        &multiply_signed_left, &multiply_signed_right,
                        &multiply_signed_result)) {
                    /* Each input is exactly 32 bits, possibly signed. Their
                     * exact mathematical product fits signed 64-bit even
                     * for INT_MIN * UINT_MAX. Compare to the *destination*
                     * range, not to ordinary C promotions. */
                    if (!emit_sp_mem(out, "ld.w", "$t0", minic_lp64_frame_value_offset(frame_plan, lhs)) ||
                        !emit_sp_mem(out, "ld.w", "$t1", minic_lp64_frame_value_offset(frame_plan, rhs)) ||
                        !emit_sp_mem(out, "ld.d", "$t2", minic_lp64_frame_value_offset(frame_plan, address)) ||
                        (!multiply_signed_left &&
                         fputs("  bstrpick.d $t0, $t0, 31, 0\n", out) == EOF) ||
                        (!multiply_signed_right &&
                         fputs("  bstrpick.d $t1, $t1, 31, 0\n", out) == EOF) ||
                        fputs("  mul.d $t3, $t0, $t1\n"
                              "  add.w $t0, $t3, $zero\n", out) == EOF ||
                        fputs(multiply_signed_result
                                  ? "  xor $t4, $t3, $t0\n"
                                  : "  srli.d $t4, $t3, 32\n", out) == EOF ||
                        fputs("  sltu $t4, $zero, $t4\n"
                              "  st.w $t0, $t2, 0\n"
                              "  move $t0, $t4\n", out) == EOF)
                        return false;
                    *needs_gp_store = true;
                    return true;
                }
                bool wide_left;
                if (minic_core_integer_overflow_u64_u32_to_i32_shape(
                        program, target, fn, ins, &wide_left)) {
                    /* Exact GNU arithmetic: the mathematical sum has up
                     * to 65 unsigned bits, even though the machine has
                     * 64-bit GPRs. Include the high carry AND the loss
                     * of signed-i32 representability after narrowing. */
                    if (!emit_sp_mem(out, wide_left ? "ld.d" : "ld.w",
                                     "$t0", minic_lp64_frame_value_offset(frame_plan, lhs)) ||
                        !emit_sp_mem(out, wide_left ? "ld.w" : "ld.d",
                                     "$t1", minic_lp64_frame_value_offset(frame_plan, rhs)) ||
                        !emit_sp_mem(out, "ld.d", "$t2",
                                     minic_lp64_frame_value_offset(frame_plan, address)) ||
                        fputs(wide_left
                              ? "  bstrpick.d $t1, $t1, 31, 0\n"
                              : "  bstrpick.d $t0, $t0, 31, 0\n", out) == EOF ||
                        fputs("  move $t3, $t0\n"
                              "  add.d $t0, $t0, $t1\n"
                              "  sltu $t3, $t0, $t3\n"
                              "  add.w $t4, $t0, $zero\n"
                              "  xor $t5, $t4, $t0\n"
                              "  sltu $t5, $zero, $t5\n"
                              "  or $t3, $t3, $t5\n"
                              "  st.w $t4, $t2, 0\n"
                              "  move $t0, $t3\n", out) == EOF)
                        return false;
                    *needs_gp_store = true;
                    return true;
                }
                bool signed_left;
                if (!minic_core_integer_overflow_mixed_add_i32_shape(
                        program, target, fn, ins, &signed_left) ||
                    !emit_sp_mem(out, "ld.d", "$t0", minic_lp64_frame_value_offset(frame_plan, lhs)) ||
                    !emit_sp_mem(out, "ld.d", "$t1", minic_lp64_frame_value_offset(frame_plan, rhs)) ||
                    !emit_sp_mem(out, "ld.d", "$t2", minic_lp64_frame_value_offset(frame_plan, address)) ||
                    fputs(signed_left
                          ? "  add.w $t0, $t0, $zero\n"
                            "  srai.d $t3, $t0, 63\n"
                          : "  add.w $t1, $t1, $zero\n"
                            "  srai.d $t3, $t1, 63\n", out) == EOF ||
                    fputs("  move $t4, $t0\n"
                          "  add.d $t0, $t0, $t1\n"
                          "  sltu $t5, $t0, $t4\n"
                          "  add.d $t3, $t3, $t5\n"
                          "  add.w $t4, $t0, $zero\n"
                          "  xor $t5, $t4, $t0\n"
                          "  sltu $t5, $zero, $t5\n"
                          "  srai.d $t4, $t4, 63\n"
                          "  xor $t3, $t3, $t4\n"
                          "  sltu $t3, $zero, $t3\n"
                          "  or $t3, $t3, $t5\n"
                          "  st.w $t0, $t2, 0\n"
                          "  move $t0, $t3\n", out) == EOF)
                    return false;
                *needs_gp_store = true;
                return true;
            }
            const int op = ins->value.integer_overflow.operator_kind;
            const bool wide = width == 8U;
            if (!emit_sp_mem(out, wide ? "ld.d" : "ld.w",
                             "$t0", minic_lp64_frame_value_offset(frame_plan, lhs)) ||
                !emit_sp_mem(out, wide ? "ld.d" : "ld.w",
                             "$t1", minic_lp64_frame_value_offset(frame_plan, rhs)) ||
                !emit_sp_mem(out, "ld.d",
                             "$t2", minic_lp64_frame_value_offset(frame_plan, address)))
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
            if (!minic_core_integer_bitscan_shape(
                    program, target, fn, ins, &width) ||
                !emit_sp_mem(out, width == 64U ? "ld.d" : "ld.w",
                             "$t0", minic_lp64_frame_value_offset(frame_plan, operand)))
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
                                 "$t2", minic_lp64_frame_value_offset(frame_plan, rhs)) ||
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
            const char *immediate_op = NULL;
            int64_t shift_count = 0;
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
            if (loong_integer_constant_rhs(fn, rhs, &shift_count) &&
                shift_count >= 0 && shift_count < (wide ? 64 : 32)) {
                immediate_op =
                    ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT
                        ? (wide ? "slli.d" : "slli.w")
                        : ls ? (wide ? "srai.d" : "srai.w")
                             : (wide ? "srli.d" : "srli.w");
            }
            if (!emit_sp_mem(out, wide ? "ld.d" : "ld.w",
                             "$t0", minic_lp64_frame_value_offset(frame_plan, lhs)) ||
                /* Signed narrow shift inputs require extension before sra. */
                (lw < 4U && !minic_loongarch64_emit_integer_normalize(out, lw, ls)) ||
                (immediate_op != NULL
                 ? fprintf(out, "  %s $t0, $t0, %" PRId64 "\n",
                           immediate_op, shift_count) < 0
                 : !emit_sp_mem(out, rw == 8U ? "ld.d" : "ld.w",
                             "$t1", minic_lp64_frame_value_offset(frame_plan, rhs)) ||
                   fputs(op, out) == EOF) ||
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
                             "$t0", minic_lp64_frame_value_offset(frame_plan, operand)) ||
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
                             "$t0", minic_lp64_frame_value_offset(frame_plan, operand)) ||
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
            int64_t constant = 0;
            bool immediate = false;
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
            if (ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY)
                immediate = minic_loongarch64_integer_rhs_immediate(
                    program, target, fn, ins, NULL, &constant);
            if ((!lhs_in_previous_gp &&
                 !emit_sp_mem(out, wide ? "ld.d" : "ld.w", "$t0",
                             minic_lp64_frame_value_offset(frame_plan, lhs))) ||
                (immediate
                 ? fprintf(out, "  addi.%c $t0, $t0, %" PRId64 "\n",
                           wide ? 'd' : 'w', constant) < 0
                 : !emit_sp_mem(out, wide ? "ld.d" : "ld.w", "$t1",
                            minic_lp64_frame_value_offset(frame_plan, rhs)) ||
                   fputs(opcode, out) == EOF) ||
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
            const char *immediate_opcode = NULL;
            int64_t constant = 0;
            if (!minic_multi_direct_integer_binary_shape(
                    program, target, fn, ins, &lw, &rw, &ls, &rs))
                return false;
            opcode = ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND
                ? "  and $t0, $t0, $t1\n"
                : ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR
                    ? "  xor $t0, $t0, $t1\n"
                    : "  or $t0, $t0, $t1\n";
            if (minic_loongarch64_integer_rhs_immediate(
                    program, target, fn, ins, NULL, &constant))
                immediate_opcode =
                    ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND
                        ? "andi"
                        : ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR
                            ? "xori" : "ori";
            if ((!lhs_in_previous_gp &&
                 !emit_sp_mem(out, lw == 8U ? "ld.d" : "ld.w",
                             "$t0", minic_lp64_frame_value_offset(frame_plan, lhs))) ||
                (immediate_opcode != NULL
                 ? fprintf(out, "  %s $t0, $t0, %" PRId64 "\n",
                           immediate_opcode, constant) < 0
                 : !emit_sp_mem(out, rw == 8U ? "ld.d" : "ld.w",
                            "$t1", minic_lp64_frame_value_offset(frame_plan, rhs)) ||
                   fputs(opcode, out) == EOF) ||
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
                             "$t0", minic_lp64_frame_value_offset(frame_plan, lhs)) ||
                !emit_sp_mem(out, wide ? "ld.d" : "ld.w",
                             "$t1", minic_lp64_frame_value_offset(frame_plan, rhs)) ||
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
                !emit_sp_mem(out, "ld.d", "$t0", minic_lp64_frame_value_offset(frame_plan, lhs)) ||
                !emit_sp_mem(out, "ld.d", "$t1", minic_lp64_frame_value_offset(frame_plan, rhs)) ||
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
                if (!emit_sp_mem(out, "ld.d", "$t0", minic_lp64_frame_value_offset(frame_plan, lhs)) ||
                    !emit_sp_mem(out, "ld.d", "$t1", minic_lp64_frame_value_offset(frame_plan, rhs)) ||
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
                             minic_lp64_frame_value_offset(frame_plan, lhs)) ||
                !emit_sp_mem(out, wide ? "ld.d" : "ld.w", "$t1",
                             minic_lp64_frame_value_offset(frame_plan, rhs)) ||
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
                             minic_lp64_frame_value_offset(frame_plan, v)) ||
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
