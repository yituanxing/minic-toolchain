#include "target/aarch64/fp_emit.h"
#include "target/aarch64/scalar_normalize.h"
#include "target/common/direct_instruction_semantics.h"
#include "target/common/lp64_scalar.h"
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>

/* All FP Core spill accesses belong to an owned frame, even when an
 * alloca changes the live physical SP. Do not re-parse emitted text. */
static bool fp_slot(FILE *out, const MinicA64FrameView *frame,
                    bool store, MinicA64FrameRegisterClass kind,
                    unsigned int reg, size_t offset) {
    const MinicA64FrameRegister operand = {kind, reg};
    const MinicA64ScratchPolicy scratch = {0U, 0U};
    return store ? minic_a64_emit_frame_store(out, frame,
                       minic_a64_owned_slot(offset), operand, scratch)
                 : minic_a64_emit_frame_load(out, frame,
                       minic_a64_owned_slot(offset), operand, scratch);
}

/* Own the native instruction sequences for the IEEE numeric family.
 * The common DirectLP64 layer owns Core operand admission and types.
 * Comparison and FP->integer leave their result in the ISA's integer
 * scratch register; other cases write their SSA slot directly. */
bool minic_aarch64_emit_fp_instruction(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    size_t offset, const MinicLP64FramePlan *frame_plan,
    const MinicA64FrameView *frame_view,
    bool *needs_gp_store) {
    if (out == NULL || program == NULL || target == NULL ||
        fn == NULL || ins == NULL || frame_plan == NULL || frame_view == NULL ||
        needs_gp_store == NULL)
        return false;
    *needs_gp_store = false;
    switch (ins->kind) {
        case MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT: {
            if (minic_type_is_long_double(ins->type)) {
                size_t dst, half;
                if (!minic_lp64_frame_plan_wide_slot(frame_plan, ins->result, &dst))
                    return false;
                for (half = 0U; half < 2U; ++half) {
                    const uint64_t bits = half == 0U
                        ? ins->value.floating128_bits.low
                        : ins->value.floating128_bits.high;
                    unsigned int shift;
                    if (fprintf(out, "  movz x9, #%" PRIu64 "\n",
                                bits & UINT64_C(0xffff)) < 0)
                        return false;
                    for (shift = 16U; shift <= 48U; shift += 16U)
                        if (((bits >> shift) & UINT64_C(0xffff)) != 0U &&
                            fprintf(out, "  movk x9, #%" PRIu64
                                         ", lsl #%u\n",
                                    (bits >> shift) & UINT64_C(0xffff),
                                    shift) < 0)
                            return false;
                    if (!fp_slot(out, frame_view, true, MINIC_A64_REG_X,
                                 9U, dst + half * 8U))
                        return false;
                }
                return true;
            }
            const uint64_t bits = ins->value.floating_bits;
            unsigned int shift;
            if ((!minic_type_is_double(ins->type) &&
                 !minic_type_is_float(ins->type)) ||
                fprintf(out, "  movz x9, #%" PRIu64 "\n",
                        bits & UINT64_C(0xffff)) < 0) return false;
            for (shift = 16U; shift <= 48U; shift += 16U) {
                if (((bits >> shift) & UINT64_C(0xffff)) != 0U &&
                    fprintf(out, "  movk x9, #%" PRIu64 ", lsl #%u\n",
                            (bits >> shift) & UINT64_C(0xffff), shift) < 0)
                    return false;
            }
            *needs_gp_store = true;
            return true;
        }
        /* Ordered IEEE comparisons: AArch64 FCMP sets C=1 on unordered.
         * 'lt' (N!=V) is WRONG for NaN. Use unsigned lo/ls conditions:
         * those are true only for ordered < or <= respectively. */
        case MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL:
        case MINIC_CORE_INSTRUCTION_DOUBLE_LESS:
        case MINIC_CORE_INSTRUCTION_DOUBLE_LESS_EQUAL: {
            const MinicCoreValueId lhs = ins->value.binary.left;
            const MinicCoreValueId rhs = ins->value.binary.right;
            const char *condition = ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL
                ? "eq" : ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_LESS ? "lo" : "ls";
            if (!minic_multi_direct_floating_shape(
                    program, target, fn, ins, NULL, NULL) ||
                !fp_slot(out, frame_view, false, MINIC_A64_REG_D, 9U,
                    (size_t)lhs * 8U) ||
                !fp_slot(out, frame_view, false, MINIC_A64_REG_D, 10U,
                    (size_t)rhs * 8U) ||
                fprintf(out, "  fcmp d9, d10\n  cset w9, %s\n", condition) < 0)
                return false;
            *needs_gp_store = true;
            return true;
        }
        /* M37: cJSON compare_double's a-b and tolerance multiplication.
         * Keep full IEEE-754 binary64 payloads in FP registers/SSA slots. */
        case MINIC_CORE_INSTRUCTION_DOUBLE_ADD:
        case MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT:
        case MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY: {
            const MinicCoreValueId lhs = ins->value.binary.left;
            const MinicCoreValueId rhs = ins->value.binary.right;
            const char *op = ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_ADD
                ? "  fadd d9, d9, d10\n"
                : ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT
                    ? "  fsub d9, d9, d10\n"
                    : "  fmul d9, d9, d10\n";
            if (!minic_multi_direct_floating_shape(
                    program, target, fn, ins, NULL, NULL) ||
                !fp_slot(out, frame_view, false, MINIC_A64_REG_D, 9U,
                    (size_t)lhs * 8U) ||
                !fp_slot(out, frame_view, false, MINIC_A64_REG_D, 10U,
                    (size_t)rhs * 8U) ||
                fputs(op, out) == EOF ||
                !fp_slot(out, frame_view, true, MINIC_A64_REG_D, 9U, offset))
                return false;
            return true;
        }
        case MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE: {
            const MinicCoreValueId lhs = ins->value.binary.left;
            const MinicCoreValueId rhs = ins->value.binary.right;
            if (!minic_multi_direct_floating_shape(
                    program, target, fn, ins, NULL, NULL) ||
                !fp_slot(out, frame_view, false, MINIC_A64_REG_D, 9U,
                    (size_t)lhs * 8U) ||
                !fp_slot(out, frame_view, false, MINIC_A64_REG_D, 10U,
                    (size_t)rhs * 8U) ||
                fputs("  fdiv d9, d9, d10\n", out) == EOF ||
                !fp_slot(out, frame_view, true, MINIC_A64_REG_D, 9U, offset))
                return false;
            return true; /* Result is already in its full 64-bit SSA slot. */
        }
        /* Unary IEEE-754 sign flip includes -0 and all NaN payloads. */
        case MINIC_CORE_INSTRUCTION_DOUBLE_NEGATE: {
            const MinicCoreValueId operand = ins->value.operand;
            if (!minic_multi_direct_floating_shape(
                    program, target, fn, ins, NULL, NULL) ||
                !fp_slot(out, frame_view, false, MINIC_A64_REG_D, 9U,
                    (size_t)operand * 8U) ||
                fputs("  fneg d9, d9\n", out) == EOF ||
                !fp_slot(out, frame_view, true, MINIC_A64_REG_D, 9U, offset))
                return false;
            return true;
        }

        /* M28: cJSON_CreateNumber converts INT_MIN to binary64, and after
         * saturation converts an in-range double to signed int.
         * Keep wider and unsigned cases fail-closed until ABI-tested. */
        /* M31: cJSON_CreateFloatArray's float element must be interpreted
         * numerically, not zero-extended as its 32-bit IEEE bit pattern. */
        case MINIC_CORE_INSTRUCTION_DOUBLE_TO_FLOAT: {
            const MinicCoreValueId source = ins->value.operand;
            if (!minic_multi_direct_floating_shape(
                    program, target, fn, ins, NULL, NULL) ||
                !fp_slot(out, frame_view, false, MINIC_A64_REG_D, 9U,
                    (size_t)source * 8U) ||
                fputs("  fcvt s9, d9\n", out) == EOF ||
                !fp_slot(out, frame_view, true, MINIC_A64_REG_S, 9U, offset))
                return false;
            return true;
        }
        case MINIC_CORE_INSTRUCTION_FLOAT_ADD:
        case MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT:
        case MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY:
        case MINIC_CORE_INSTRUCTION_FLOAT_DIVIDE: {
            const MinicCoreValueId lhs = ins->value.binary.left;
            const MinicCoreValueId rhs = ins->value.binary.right;
            const char *op = ins->kind == MINIC_CORE_INSTRUCTION_FLOAT_ADD
                ? "  fadd s9, s9, s10\n"
                : ins->kind == MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT
                    ? "  fsub s9, s9, s10\n"
                    : ins->kind == MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY
                        ? "  fmul s9, s9, s10\n"
                        : "  fdiv s9, s9, s10\n";
            if (!minic_multi_direct_floating_shape(
                    program, target, fn, ins, NULL, NULL) ||
                !fp_slot(out, frame_view, false, MINIC_A64_REG_S, 9U,
                    (size_t)lhs * 8U) ||
                !fp_slot(out, frame_view, false, MINIC_A64_REG_S, 10U,
                    (size_t)rhs * 8U) ||
                fputs(op, out) == EOF ||
                !fp_slot(out, frame_view, true, MINIC_A64_REG_S, 9U, offset))
                return false;
            return true;
        }
        case MINIC_CORE_INSTRUCTION_FLOAT_TO_DOUBLE: {
            const MinicCoreValueId src = ins->value.operand;
            if (!minic_multi_direct_floating_shape(
                    program, target, fn, ins, NULL, NULL) ||
                !fp_slot(out, frame_view, false, MINIC_A64_REG_S, 9U,
                    (size_t)src * 8U) ||
                fputs("  fcvt d9, s9\n", out) == EOF ||
                !fp_slot(out, frame_view, true, MINIC_A64_REG_D, 9U, offset))
                return false;
            return true;
        }
        case MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE: {
            const MinicCoreValueId src = ins->value.operand;
            size_t width;
            bool sign;
            /* Numeric C conversion: AArch64 has signed AND unsigned 32/64
             * integer-to-double instructions. Narrow source values retain
             * their Core signedness through explicit normalization. */
            if (!minic_multi_direct_floating_shape(
                    program, target, fn, ins, &width, &sign) ||
                !fp_slot(out, frame_view, false,
                    width == 8U ? MINIC_A64_REG_X : MINIC_A64_REG_W,
                    9U, (size_t)src * 8U) ||
                (width < 4U && !minic_aarch64_emit_integer_normalize(out, width, sign)) ||
                fputs(width == 8U
                          ? (sign ? "  scvtf d9, x9\n"
                                  : "  ucvtf d9, x9\n")
                          : (sign ? "  scvtf d9, w9\n"
                                  : "  ucvtf d9, w9\n"), out) == EOF ||
                !fp_slot(out, frame_view, true, MINIC_A64_REG_D, 9U, offset))
                return false;
            return true;
        }
        case MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER: {
            const MinicCoreValueId src = ins->value.operand;
            size_t width;
            bool sign;
            const char *conversion;
            /* Lua's lua_Integer is signed LP64; C double-to-integer
             * conversion truncates toward zero. Keep unsigned64 fail-closed
             * until its full-range ABI oracle is introduced. */
            if (!minic_multi_direct_floating_shape(
                    program, target, fn, ins, &width, &sign))
                return false;
            /* A64 natively supports unsigned 64-bit binary64 conversion.
             * C-defined finite inputs truncate toward zero; NaN/overflow
             * inputs remain outside the C conversion contract. */
            conversion = width == 8U
                ? (sign ? "  fcvtzs x9, d9\n"
                        : "  fcvtzu x9, d9\n")
                : sign ? "  fcvtzs w9, d9\n"
                       : "  fcvtzu w9, d9\n";
            if (!fp_slot(out, frame_view, false, MINIC_A64_REG_D,
                    9U, (size_t)src * 8U) ||
                fputs(conversion, out) == EOF ||
                (width < 4U && !minic_aarch64_emit_integer_normalize(out, width, sign)))
                return false;
            *needs_gp_store = true;
            return true;
        }
    default:
        return false;
    }
}
