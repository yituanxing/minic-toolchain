#include "target/loongarch64/fp_emit.h"
#include "target/loongarch64/scalar_normalize.h"
#include "target/common/direct_instruction_semantics.h"
#include "target/common/lp64_scalar.h"
#include "target/loongarch64/frame_emit.h"
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>

#define emit_sp_mem(_out, _op, _reg, _offset) \
    minic_loongarch64_emit_frame_mem((_out), (_op), (_reg), (_offset), dynamic_frame)

/* Own the native instruction sequences for the IEEE numeric family.
 * The common DirectLP64 layer owns Core operand admission and types.
 * Comparison and FP->integer leave their result in the ISA's integer
 * scratch register; other cases write their SSA slot directly. */
bool minic_loongarch64_emit_fp_instruction(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    size_t offset, size_t id, bool dynamic_frame, const MinicLP64FramePlan *frame_plan,
    bool *needs_gp_store) {
    if (out == NULL || program == NULL || target == NULL ||
        fn == NULL || ins == NULL || frame_plan == NULL ||
        needs_gp_store == NULL)
        return false;
    *needs_gp_store = false;
    switch (ins->kind) {
        case MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT:
            if (minic_type_is_long_double(ins->type)) {
                size_t dst;
                if (!minic_lp64_frame_plan_wide_slot(frame_plan, ins->result, &dst) ||
                    fprintf(out, "  li.d $t0, 0x%016" PRIx64 "\n",
                            ins->value.floating128_bits.low) < 0 ||
                    !emit_sp_mem(out, "st.d", "$t0", dst) ||
                    fprintf(out, "  li.d $t0, 0x%016" PRIx64 "\n",
                            ins->value.floating128_bits.high) < 0 ||
                    !emit_sp_mem(out, "st.d", "$t0", dst + 8U))
                    return false;
                return true;
            }
            if ((!minic_type_is_double(ins->type) &&
                 !minic_type_is_float(ins->type)) ||
                fprintf(out, "  li.d $t0, 0x%016" PRIx64 "\n",
                        ins->value.floating_bits) < 0) return false;
            *needs_gp_store = true;
            return true;
        /* LoongArch FCC ordered comparisons naturally reject unordered
         * operands: fcmp.ceq/clt/cle is false if either operand is NaN. */
        case MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL:
        case MINIC_CORE_INSTRUCTION_DOUBLE_LESS:
        case MINIC_CORE_INSTRUCTION_DOUBLE_LESS_EQUAL: {
            const MinicCoreValueId lhs = ins->value.binary.left;
            const MinicCoreValueId rhs = ins->value.binary.right;
            const char *op = ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL
                ? "fcmp.ceq.d" :
                ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_LESS
                    ? "fcmp.clt.d" : "fcmp.cle.d";
            if (!minic_multi_direct_floating_shape(
                    program, target, fn, ins, NULL, NULL) ||
                !emit_sp_mem(out, "fld.d", "$ft0", minic_lp64_frame_value_offset(frame_plan, lhs)) ||
                !emit_sp_mem(out, "fld.d", "$ft1", minic_lp64_frame_value_offset(frame_plan, rhs)) ||
                fprintf(out, "  %s $fcc0, $ft0, $ft1\n"
                             "  movcf2gr $t0, $fcc0\n", op) < 0)
                return false;
            *needs_gp_store = true;
            return true;
        }
        /* M37: source-observed cJSON binary64 subtract and multiply. */
        case MINIC_CORE_INSTRUCTION_DOUBLE_ADD:
        case MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT:
        case MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY: {
            const MinicCoreValueId lhs = ins->value.binary.left;
            const MinicCoreValueId rhs = ins->value.binary.right;
            const char *op = ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_ADD
                ? "  fadd.d $ft0, $ft0, $ft1\n"
                : ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT
                    ? "  fsub.d $ft0, $ft0, $ft1\n"
                    : "  fmul.d $ft0, $ft0, $ft1\n";
            if (!minic_multi_direct_floating_shape(
                    program, target, fn, ins, NULL, NULL) ||
                !emit_sp_mem(out, "fld.d", "$ft0", minic_lp64_frame_value_offset(frame_plan, lhs)) ||
                !emit_sp_mem(out, "fld.d", "$ft1", minic_lp64_frame_value_offset(frame_plan, rhs)) ||
                fputs(op, out) == EOF ||
                !emit_sp_mem(out, "fst.d", "$ft0", offset))
                return false;
            return true;
        }
        case MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE: {
            const MinicCoreValueId lhs = ins->value.binary.left;
            const MinicCoreValueId rhs = ins->value.binary.right;
            if (!minic_multi_direct_floating_shape(
                    program, target, fn, ins, NULL, NULL) ||
                !emit_sp_mem(out, "fld.d", "$ft0", minic_lp64_frame_value_offset(frame_plan, lhs)) ||
                !emit_sp_mem(out, "fld.d", "$ft1", minic_lp64_frame_value_offset(frame_plan, rhs)) ||
                fputs("  fdiv.d $ft0, $ft0, $ft1\n", out) == EOF ||
                !emit_sp_mem(out, "fst.d", "$ft0", offset))
                return false;
            return true;
        }
        case MINIC_CORE_INSTRUCTION_DOUBLE_NEGATE: {
            const MinicCoreValueId operand = ins->value.operand;
            if (!minic_multi_direct_floating_shape(
                    program, target, fn, ins, NULL, NULL) ||
                !emit_sp_mem(out, "fld.d", "$ft0", minic_lp64_frame_value_offset(frame_plan, operand)) ||
                fputs("  fneg.d $ft0, $ft0\n", out) == EOF ||
                !emit_sp_mem(out, "fst.d", "$ft0", offset))
                return false;
            return true;
        }

        /* M28: IEEE integer/double conversions used by cJSON's saturated
         * valueint assignment. Only signed 32-bit conversion is admitted. */
        /* M31: numeric IEEE binary32->binary64 promotion for float arrays.
         * Reinterpretation/bitcast is incorrect: perform fcvt.d.s. */
        case MINIC_CORE_INSTRUCTION_DOUBLE_TO_FLOAT: {
            const MinicCoreValueId source = ins->value.operand;
            if (!minic_multi_direct_floating_shape(
                    program, target, fn, ins, NULL, NULL) ||
                !emit_sp_mem(out, "fld.d", "$ft0", minic_lp64_frame_value_offset(frame_plan, source)) ||
                fputs("  fcvt.s.d $ft0, $ft0\n", out) == EOF ||
                !emit_sp_mem(out, "fst.s", "$ft0", offset))
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
                ? "  fadd.s $ft0, $ft0, $ft1\n"
                : ins->kind == MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT
                    ? "  fsub.s $ft0, $ft0, $ft1\n"
                    : ins->kind == MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY
                        ? "  fmul.s $ft0, $ft0, $ft1\n"
                        : "  fdiv.s $ft0, $ft0, $ft1\n";
            if (!minic_multi_direct_floating_shape(
                    program, target, fn, ins, NULL, NULL) ||
                !emit_sp_mem(out, "fld.s", "$ft0", minic_lp64_frame_value_offset(frame_plan, lhs)) ||
                !emit_sp_mem(out, "fld.s", "$ft1", minic_lp64_frame_value_offset(frame_plan, rhs)) ||
                fputs(op, out) == EOF ||
                !emit_sp_mem(out, "fst.s", "$ft0", offset))
                return false;
            return true;
        }
        case MINIC_CORE_INSTRUCTION_FLOAT_TO_DOUBLE: {
            const MinicCoreValueId src = ins->value.operand;
            if (!minic_multi_direct_floating_shape(
                    program, target, fn, ins, NULL, NULL) ||
                !emit_sp_mem(out, "fld.s", "$ft0", minic_lp64_frame_value_offset(frame_plan, src)) ||
                fputs("  fcvt.d.s $ft0, $ft0\n", out) == EOF ||
                !emit_sp_mem(out, "fst.d", "$ft0", offset))
                return false;
            return true;
        }
        case MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE: {
            const MinicCoreValueId src = ins->value.operand;
            size_t width;
            bool sign;
            if (!minic_multi_direct_floating_shape(
                    program, target, fn, ins, &width, &sign) ||
                !emit_sp_mem(out, width == 8U ? "ld.d" : "ld.w",
                             "$t0", minic_lp64_frame_value_offset(frame_plan, src)) ||
                (width < 4U && !minic_loongarch64_emit_integer_normalize(out, width, sign)))
                return false;
            if (width == 8U && !sign) {
                /* LoongArch base FP has signed ffint.d.l, but not an
                 * unsigned 64-bit scalar conversion. Split at 32 bits:
                 * both pieces are exact in FP64, and fadd rounds ONCE.
                 * This avoids the double-rounding trap of (x>>1)*2. */
                if (fputs("  srli.d $t1, $t0, 32\n"
                          "  bstrpick.d $t0, $t0, 31, 0\n"
                          "  movgr2fr.d $ft0, $t1\n"
                          "  ffint.d.l $ft0, $ft0\n"
                          "  movgr2fr.d $ft1, $t0\n"
                          "  ffint.d.l $ft1, $ft1\n"
                          "  li.d $t2, 0x41f0000000000000\n"
                          "  movgr2fr.d $ft2, $t2\n"
                          "  fmul.d $ft0, $ft0, $ft2\n"
                          "  fadd.d $ft0, $ft0, $ft1\n", out) == EOF)
                    return false;
            } else if (width == 4U && !sign) {
                /* ld.w sign-extends on LP64; zero-extend unsigned int
                 * before signed-long-to-double conversion. */
                if (fputs("  bstrpick.d $t0, $t0, 31, 0\n"
                          "  movgr2fr.d $ft0, $t0\n"
                          "  ffint.d.l $ft0, $ft0\n", out) == EOF)
                    return false;
            } else {
                if (fputs(width == 8U
                              ? "  movgr2fr.d $ft0, $t0\n"
                                "  ffint.d.l $ft0, $ft0\n"
                              : "  movgr2fr.w $ft0, $t0\n"
                                "  ffint.d.w $ft0, $ft0\n", out) == EOF)
                    return false;
            }
            if (!emit_sp_mem(out, "fst.d", "$ft0", offset))
                return false;
            return true;
        }
        case MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER: {
            const MinicCoreValueId src = ins->value.operand;
            size_t width;
            bool sign;
            const char *conversion;
            /* LoongArch's ftintrz.l.d converts binary64 to signed64.
             * That also covers the entire unsigned32 range: retain its
             * low 32 bits for the Core result. Unsigned64 remains gated. */
            if (!minic_multi_direct_floating_shape(
                    program, target, fn, ins, &width, &sign))
                return false;
            if (width == 8U && !sign) {
                /* LP64D supplies signed ftintrz.l.d only. For C-defined
                 * uint64 conversions, translate the upper half of the
                 * unsigned domain by 2^63 and restore its high bit. */
                if (!emit_sp_mem(out, "fld.d", "$ft0",
                                 minic_lp64_frame_value_offset(frame_plan, src)) ||
                    fprintf(out,
                       "  li.d $t1, 0x43e0000000000000\n"
                       "  movgr2fr.d $ft1, $t1\n"
                       "  fcmp.clt.d $fcc0, $ft0, $ft1\n"
                       "  movcf2gr $t1, $fcc0\n"
                       "  bnez $t1, .Lmini_loong_u64_f%zu_v%u_low\n"
                       "  fsub.d $ft0, $ft0, $ft1\n"
                       "  ftintrz.l.d $ft0, $ft0\n"
                       "  movfr2gr.d $t0, $ft0\n"
                       "  li.d $t1, 0x8000000000000000\n"
                       "  or $t0, $t0, $t1\n"
                       "  b .Lmini_loong_u64_f%zu_v%u_done\n"
                       ".Lmini_loong_u64_f%zu_v%u_low:\n"
                       "  ftintrz.l.d $ft0, $ft0\n"
                       "  movfr2gr.d $t0, $ft0\n"
                       ".Lmini_loong_u64_f%zu_v%u_done:\n",
                       id, (unsigned int)ins->result,
                       id, (unsigned int)ins->result,
                       id, (unsigned int)ins->result,
                       id, (unsigned int)ins->result) < 0)
                    return false;
                *needs_gp_store = true;
            return true;
            }
            conversion = width == 4U && sign
                ? "  ftintrz.w.d $ft0, $ft0\n"
                  "  movfr2gr.s $t0, $ft0\n"
                : "  ftintrz.l.d $ft0, $ft0\n"
                  "  movfr2gr.d $t0, $ft0\n";
            if (!emit_sp_mem(out, "fld.d", "$ft0", minic_lp64_frame_value_offset(frame_plan, src)) ||
                fputs(conversion, out) == EOF ||
                (width == 4U && !sign &&
                 fputs("  bstrpick.d $t0, $t0, 31, 0\n", out) == EOF) ||
                (width < 4U && !minic_loongarch64_emit_integer_normalize(out, width, sign)))
                return false;
            *needs_gp_store = true;
            return true;
        }
    default:
        return false;
    }
}

#undef emit_sp_mem
