#include "target/riscv32/rv32_internal.h"
#include "target/riscv/instruction_select.h"
#include "target/riscv/typed_memory.h"
#include "target/riscv/word_operations.h"
#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>

/* GCC __builtin_*_overflow computes in infinite signed precision and then
 * checks whether the value fits the destination type. In particular,
 * signed int + unsigned int -> signed int must not use ordinary C promotion.
 * Two sign/zero-extended RV32 input words fit in an exact 64-bit scratch
 * value for add/sub; multiplication uses the existing libgcc __muldi3. */
static bool rv32_emit_mixed_checked_overflow(FILE *out,
                                             const MinicCoreFunction *f,
                                             const Rv32Frame *fr,
                                             const MinicCoreInstruction *ins,
                                             MinicType result_type,
                                             size_t address_alignment) {
    const MinicCoreValueId lhs = ins->value.integer_overflow.left;
    const MinicCoreValueId rhs = ins->value.integer_overflow.right;
    unsigned left_bits, right_bits, result_bits;
    bool left_signed, right_signed, result_signed;
    const char *store;
    /* The kernel also uses __builtin_add_overflow(unsigned long long,
     * unsigned int, unsigned int *).  The mathematical sum fits one
     * zero-extended XLEN word exactly iff the high input word is zero and
     * the low-word addition does not carry.  Keep this case independent of
     * the two-narrow-operands path: rv32_scalar deliberately rejects a
     * two-word input. */
    if (ins->value.integer_overflow.operator_kind ==
            MINIC_CORE_INTEGER_OVERFLOW_ADD &&
        ((rv32_is_wide_integer(f, f->values[lhs].type) &&
          minic_type_is_unsigned_integer(f->values[lhs].type) &&
          rv32_scalar(f, f->values[rhs].type, &right_bits, &right_signed) &&
          !right_signed && right_bits == 32U) ||
         (rv32_is_wide_integer(f, f->values[rhs].type) &&
          minic_type_is_unsigned_integer(f->values[rhs].type) &&
          rv32_scalar(f, f->values[lhs].type, &left_bits, &left_signed) &&
          !left_signed && left_bits == 32U)) &&
        rv32_scalar(f, result_type, &result_bits, &result_signed) &&
        !result_signed && result_bits == 32U) {
        const bool wide_left =
            rv32_is_wide_integer(f, f->values[lhs].type);
        const MinicCoreValueId wide = wide_left ? lhs : rhs;
        const MinicCoreValueId narrow = wide_left ? rhs : lhs;
        return rv32_pair_slot(out, fr, wide, "t0", "t1", true) &&
               rv32_slot(out, fr, narrow, "t2", true) &&
               fprintf(out,
                   "  add t3, t0, t2\n"
                   "  sltu t4, t3, t0\n"
                   "  or t4, t4, t1\n"
                   "  snez t4, t4\n") >= 0 &&
               rv32_slot(out, fr,
                         ins->value.integer_overflow.result_address,
                         "t0", true) &&
               minic_riscv_emit_typed_memory(
                   out, true, 32U, 4U, false, "sw",
                   "t3", "t0", "t5", address_alignment, "rv32_overflow") &&
               rv32_slot(out, fr, ins->result, "t4", false);
    }
    if (!rv32_scalar(f, f->values[lhs].type, &left_bits, &left_signed) ||
        !rv32_scalar(f, f->values[rhs].type, &right_bits, &right_signed) ||
        !rv32_scalar(f, result_type, &result_bits, &result_signed) ||
        left_bits < 8U || right_bits < 8U || result_bits < 8U ||
        left_bits > 32U || right_bits > 32U || result_bits > 32U ||
        !rv32_slot(out, fr, lhs, "t0", true) ||
        !rv32_slot(out, fr, rhs, "t1", true) ||
        fprintf(out, left_signed ? "  srai t3, t0, 31\n" :
                                   "  li t3, 0\n") < 0 ||
        fprintf(out, right_signed ? "  srai t4, t1, 31\n" :
                                    "  li t4, 0\n") < 0)
        return false;
    switch (ins->value.integer_overflow.operator_kind) {
    case MINIC_CORE_INTEGER_OVERFLOW_ADD:
        if (fprintf(out,
            "  add t2, t0, t1\n  sltu t5, t2, t0\n"
            "  add t3, t3, t4\n  add t3, t3, t5\n") < 0)
            return false;
        break;
    case MINIC_CORE_INTEGER_OVERFLOW_SUBTRACT:
        if (fprintf(out,
            "  sub t2, t0, t1\n  sltu t5, t0, t1\n"
            "  sub t3, t3, t4\n  sub t3, t3, t5\n") < 0)
            return false;
        break;
    case MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY:
        if (fprintf(out,
            "  mv a0, t0\n  mv a1, t3\n"
            "  mv a2, t1\n  mv a3, t4\n"
            "  call __muldi3\n  mv t2, a0\n  mv t3, a1\n") < 0)
            return false;
        break;
    default:
        return false;
    }
    /* Compare the full mathematical result to the two-word sign/zero
     * extension of the value actually written to the destination. */
    if (fprintf(out, "  mv t5, t2\n") < 0 ||
        !rv32_canon(out, f, result_type, "t2") ||
        fprintf(out,
            "  xor t5, t5, t2\n"
            "  %s\n"
            "  xor t4, t4, t3\n"
            "  or t4, t4, t5\n  snez t4, t4\n",
            result_signed ? "srai t4, t2, 31" : "li t4, 0") < 0)
        return false;
    store = result_bits == 8U ? "sb" : result_bits == 16U ? "sh" : "sw";
    return rv32_slot(out, fr, ins->value.integer_overflow.result_address,
                     "t0", true) &&
           minic_riscv_emit_typed_memory(
               out, true, 32U, result_bits / 8U, false, store,
               "t2", "t0", "t5", address_alignment, "rv32_overflow") &&
           rv32_slot(out, fr, ins->result, "t4", false);
}

/* ILP32 long long remains one Core value transported in two 32-bit words.
 * Carry/borrow and sign rules are computed across both words; 64-bit MUL
 * and DIV use the same pinned original compiler-runtime symbols as the
 * ordinary RV32I wide-integer lowering. */
static bool rv32_emit_wide_checked_overflow(FILE *out,
                                            const Rv32Frame *fr,
                                            const MinicCoreInstruction *ins,
                                            bool is_unsigned,
                                            size_t address_alignment) {
    const MinicCoreValueId lhs = ins->value.integer_overflow.left;
    const MinicCoreValueId rhs = ins->value.integer_overflow.right;
    const MinicCoreValueId address = ins->value.integer_overflow.result_address;
    if (!rv32_pair_slot(out, fr, lhs, "t0", "t1", true) ||
        !rv32_pair_slot(out, fr, rhs, "t2", "t3", true))
        return false;
    switch (ins->value.integer_overflow.operator_kind) {
    case MINIC_CORE_INTEGER_OVERFLOW_ADD:
        if (fprintf(out,
            "  add t4, t2, t0\n  sltu t6, t4, t0\n"
            "  add t5, t1, t3\n  sltu a0, t5, t1\n"
            "  add t5, t5, t6\n  sltu a1, t5, t6\n"
            "  or a0, a0, a1\n") < 0)
            return false;
        if (!is_unsigned &&
            fprintf(out,
                "  xor a0, t1, t3\n  xori a0, a0, -1\n"
                "  xor a1, t1, t5\n  and a0, a0, a1\n"
                "  srli a0, a0, 31\n") < 0)
            return false;
        break;
    case MINIC_CORE_INTEGER_OVERFLOW_SUBTRACT:
        if (fprintf(out,
            "  sub t4, t0, t2\n  sltu t6, t0, t2\n"
            "  sub t5, t1, t3\n  sltu a0, t1, t3\n"
            "  sltu a1, t5, t6\n  sub t5, t5, t6\n"
            "  or a0, a0, a1\n") < 0)
            return false;
        if (!is_unsigned &&
            fprintf(out,
                "  xor a0, t1, t3\n  xor a1, t1, t5\n"
                "  and a0, a0, a1\n  srli a0, a0, 31\n") < 0)
            return false;
        break;
    case MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY:
        if (fprintf(out,
            "  mv a0, t0\n  mv a1, t1\n"
            "  mv a2, t2\n  mv a3, t3\n"
            "  call __muldi3\n"
            "  mv t4, a0\n  mv t5, a1\n") < 0 ||
            !rv32_slot(out, fr, address, "t0", true) ||
            !minic_riscv_emit_typed_word_pair(
                out, true, 32U, "t4", "t5", "t0", "t6",
                address_alignment, "rv32_overflow") ||
            !rv32_pair_slot(out, fr, rhs, "t2", "t3", true) ||
            fprintf(out, "  or t6, t2, t3\n  beqz t6, 1f\n") < 0 ||
            !rv32_slot(out, fr, address, "t0", true) ||
            fprintf(out,
                "  mv a2, t2\n  mv a3, t3\n") < 0 ||
            !minic_riscv_emit_typed_word_pair(
                out, false, 32U, "a0", "a1", "t0", "t6",
                address_alignment, "rv32_overflow") ||
            fprintf(out, "  call %s\n",
                    is_unsigned ? "__udivdi3" : "__divdi3") < 0 ||
            !rv32_pair_slot(out, fr, lhs, "t0", "t1", true) ||
            fprintf(out,
                "  xor a0, a0, t0\n  xor a1, a1, t1\n"
                "  or a0, a0, a1\n  snez a0, a0\n") < 0)
            return false;
        if (!is_unsigned &&
            (!rv32_pair_slot(out, fr, rhs, "t2", "t3", true) ||
             fprintf(out,
                 "  li t4, 0x80000000\n  bne t1, t4, 2f\n"
                 "  bnez t0, 2f\n  li t4, -1\n"
                 "  bne t2, t4, 2f\n  bne t3, t4, 2f\n"
                 "  li a0, 1\n2:\n") < 0))
            return false;
        return fprintf(out, "  j 3f\n1:\n  li a0, 0\n3:\n") >= 0 &&
               rv32_slot(out, fr, ins->result, "a0", false);
    default:
        return false;
    }
    return rv32_slot(out, fr, address, "t0", true) &&
           minic_riscv_emit_typed_word_pair(
               out, true, 32U, "t4", "t5", "t0", "t6",
               address_alignment, "rv32_overflow") &&
           rv32_slot(out, fr, ins->result, "a0", false);
}

/* Checked arithmetic is a Core operation, not a Linux-specific intrinsic.
 * RV32I uses base-I carry/sign tests and the same GNU libgcc arithmetic
 * helpers already selected by ordinary RV32 scalar multiplication/division.
 * Keep operands in their owned Core frame slots across every libcall. */
static bool rv32_emit_checked_overflow(FILE *out,
                                       const MinicC0Program *program,
                                       const MinicTargetInfo *target,
                                       const MinicCoreFunction *f,
                                       const Rv32Frame *fr,
                                       const MinicCoreInstruction *ins) {
    const MinicCoreValueId lhs = ins->value.integer_overflow.left;
    const MinicCoreValueId rhs = ins->value.integer_overflow.right;
    const MinicCoreValueId address = ins->value.integer_overflow.result_address;
    MinicType result_type;
    unsigned bits;
    bool is_signed;
    const char *store;
    size_t address_alignment;
    if (program == NULL || target == NULL || target->data_layout == NULL ||
        lhs >= f->value_count || rhs >= f->value_count ||
        address >= f->value_count || ins->result >= f->value_count ||
        !minic_type_equal(ins->type, minic_type_bool()) ||
        !minic_type_pointee(f->values[address].type, &result_type))
        return false;
    address_alignment = minic_riscv_address_alignment(
        program, target->data_layout, f, address);
    if (!minic_type_equal(f->values[lhs].type, result_type) ||
        !minic_type_equal(f->values[rhs].type, result_type)) {
        if (rv32_scalar_storage_words(f, result_type, &bits) && bits == 1U &&
            rv32_emit_mixed_checked_overflow(
                out, f, fr, ins, result_type, address_alignment))
            return true;
        if (getenv("MINIC_RV32_OVERFLOW_TRACE") != NULL)
            (void)fprintf(stderr,
                "RV32_OVERFLOW_TYPE lhs_rank=%d lhs_sign=%d rhs_rank=%d rhs_sign=%d result_rank=%d result_sign=%d operator=%d\n",
                (int)f->values[lhs].type.integer_rank,
                (int)f->values[lhs].type.integer_sign,
                (int)f->values[rhs].type.integer_rank,
                (int)f->values[rhs].type.integer_sign,
                (int)result_type.integer_rank,
                (int)result_type.integer_sign,
                (int)ins->value.integer_overflow.operator_kind);
        return false;
    }
    if (rv32_is_wide_integer(f, result_type))
        return rv32_emit_wide_checked_overflow(
            out, fr, ins, minic_type_is_unsigned_integer(result_type),
            address_alignment);
    if (
        !rv32_scalar(f, result_type, &bits, &is_signed) ||
        bits < 8U || bits > 32U ||
        !rv32_slot(out, fr, lhs, "t0", true) ||
        !rv32_slot(out, fr, rhs, "t1", true))
        return false;
    store = bits == 8U ? "sb" : bits == 16U ? "sh" : "sw";
    switch (ins->value.integer_overflow.operator_kind) {
    case MINIC_CORE_INTEGER_OVERFLOW_ADD:
        if (fprintf(out, "  add t2, t0, t1\n") < 0) return false;
        if (bits == 32U) {
            if (is_signed) {
                if (fprintf(out,
                    "  xor t4, t0, t1\n  xori t4, t4, -1\n"
                    "  xor t5, t0, t2\n  and t4, t4, t5\n"
                    "  srli t4, t4, 31\n") < 0) return false;
            } else if (fprintf(out, "  sltu t4, t2, t0\n") < 0)
                return false;
        }
        break;
    case MINIC_CORE_INTEGER_OVERFLOW_SUBTRACT:
        if (fprintf(out, "  sub t2, t0, t1\n") < 0) return false;
        if (bits == 32U) {
            if (is_signed) {
                if (fprintf(out,
                    "  xor t4, t0, t1\n  xor t5, t0, t2\n"
                    "  and t4, t4, t5\n  srli t4, t4, 31\n") < 0)
                    return false;
            } else if (fprintf(out, "  sltu t4, t0, t1\n") < 0)
                return false;
        }
        break;
    case MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY:
        if (fprintf(out,
            "  mv a0, t0\n  mv a1, t1\n  call __mulsi3\n"
            "  mv t2, a0\n") < 0) return false;
        if (bits == 32U) {
            /* Preserve the wrapped result before a second libcall, then
             * compare its quotient against the ORIGINAL frame operand.
             * The signed INT_MIN/-1 division overflow needs its own flag. */
            if (!rv32_slot(out, fr, address, "t0", true) ||
                !minic_riscv_emit_typed_memory(
                    out, true, 32U, 4U, false, "sw", "t2", "t0", "t5",
                    address_alignment, "rv32_overflow") ||
                !rv32_slot(out, fr, rhs, "t1", true) ||
                fprintf(out, "  beqz t1, 1f\n") < 0 ||
                !rv32_slot(out, fr, address, "t0", true) ||
                fprintf(out,
                    "  mv a1, t1\n") < 0 ||
                !minic_riscv_emit_typed_memory(
                    out, false, 32U, 4U, is_signed, "lw",
                    "t2", "t0", "t5", address_alignment, "rv32_overflow") ||
                fprintf(out, "  mv a0, t2\n  call %s\n",
                        is_signed ? "__divsi3" : "__udivsi3") < 0 ||
                !rv32_slot(out, fr, lhs, "t0", true) ||
                fprintf(out, "  xor t4, a0, t0\n  snez t4, t4\n") < 0)
                return false;
            if (is_signed &&
                (!rv32_slot(out, fr, rhs, "t1", true) ||
                 fprintf(out,
                    "  li t2, -1\n  bne t1, t2, 2f\n"
                    "  li t2, 0x80000000\n  bne t0, t2, 2f\n"
                    "  li t4, 1\n2:\n") < 0))
                return false;
            return fprintf(out, "  j 3f\n1:\n  li t4, 0\n3:\n") >= 0 &&
                   rv32_slot(out, fr, ins->result, "t4", false);
        }
        break;
    default:
        return false;
    }
    if (bits < 32U) {
        /* Both narrow operands are canonicalized on load. Their sum,
         * difference and product fit an exact 32-bit scratch result. */
        if (fprintf(out, "  mv t3, t2\n") < 0 ||
            !rv32_canon(out, f, result_type, "t3") ||
            fprintf(out,
                    "  xor t4, t2, t3\n  snez t4, t4\n"
                    "  mv t2, t3\n") < 0) return false;
    }
    return rv32_slot(out, fr, address, "t0", true) &&
           minic_riscv_emit_typed_memory(
               out, true, 32U, bits / 8U, false, store,
               "t2", "t0", "t5", address_alignment, "rv32_overflow") &&
           rv32_slot(out, fr, ins->result, "t4", false);
}

/* RV32 integer machine selection and ILP32 normalization. Common
 * RISC-V opcode and runtime symbol identities remain XLEN-parametric. */
bool rv32_emit_integer_instruction(FILE *out,
                                   const MinicC0Program *program,
                                   const MinicTargetInfo *target,
                                   const MinicCoreFunction *f,
                                   const Rv32Frame *fr,
                                   const MinicCoreInstruction *ins,
                                   const char *symbol) {
    const char *op = NULL;
    MinicType lhs;
    unsigned bits;
    bool is_signed;
    if (out == NULL || f == NULL || fr == NULL || ins == NULL) return false;
    /* Keep Core's wide arithmetic/compare/shift lowering as one machine
     * family; the scalar path must never truncate a 2*XLEN value. */
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
    case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:
    case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE:
    case MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER:
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT:
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT:
    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:
        if (rv32_is_wide_integer(f, ins->type))
            return rv32_emit_wide_integer(out, f, fr, ins, symbol);
        break;
    case MINIC_CORE_INSTRUCTION_INTEGER_LESS:
    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:
        if (ins->value.binary.left < f->value_count &&
            rv32_is_wide_integer(f, f->values[ins->value.binary.left].type))
            return rv32_emit_wide_integer(out, f, fr, ins, symbol);
        break;
    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:
        if (ins->value.operand < f->value_count &&
            rv32_is_wide_integer(f, f->values[ins->value.operand].type))
            return rv32_emit_wide_integer(out, f, fr, ins, symbol);
        break;
    default: break;
    }
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW:
        return rv32_emit_checked_overflow(out, program, target, f, fr, ins);
    case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT: {
        unsigned words;
        if (!rv32_scalar_storage_words(f, ins->type, &words))
            return false;
        if (words == 2U) {
            uint64_t raw = (uint64_t)ins->value.integer_value;
            return fprintf(out, "  li t0, %" PRIu32 "\n  li t1, %" PRIu32 "\n",
                           (uint32_t)raw, (uint32_t)(raw >> 32U)) >= 0 &&
                   rv32_pair_slot(out, fr, ins->result, "t0", "t1", false);
        }
        return rv32_scalar(f, ins->type, &bits, &is_signed) &&
               fprintf(out, "  li t0, %" PRId64 "\n",
                       ins->value.integer_value) >= 0 &&
               rv32_canon(out, f, ins->type, "t0") &&
               rv32_slot(out, fr, ins->result, "t0", false);
    }
    /* Core owns widths; both XLEN variants share base-I opcode names. */
    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
    case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT:
        op = minic_riscv_scalar_binary_opcode(ins->kind, false);
        break;
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT:
    case MINIC_CORE_INSTRUCTION_INTEGER_LESS:
    case MINIC_CORE_INSTRUCTION_POINTER_LESS:
        if (ins->value.binary.left >= f->value_count) return false;
        lhs = f->values[ins->value.binary.left].type;
        if (!rv32_scalar(f, lhs, &bits, &is_signed)) return false;
        op = minic_riscv_scalar_binary_opcode(
            ins->kind, ins->kind == MINIC_CORE_INSTRUCTION_POINTER_LESS ||
                           !is_signed);
        break;
    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:
        return rv32_load2(out, fr, ins) &&
               fprintf(out, "  xor t0, t0, t1\n  seqz t0, t0\n") >= 0 &&
               rv32_slot(out, fr, ins->result, "t0", false);
    case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE:
    case MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER: {
        /* Compiler target lowering only chooses an ABI-compatible libcall.
         * Actual arithmetic lives in the pinned GNU libgcc 14.2.0 sources,
         * not a second implementation in MiniC's instruction generator. */
        if (!rv32_scalar(f, ins->type, &bits, &is_signed))
            return false;
        MinicRiscvIntegerPlan plan;
        if (!minic_riscv_plan_integer_arithmetic(
                fr->isa, ins->kind, !is_signed, 32U, &plan))
            return false;
        if (plan.strategy == MINIC_RISCV_INTEGER_NATIVE_M)
            return rv32_load2(out, fr, ins) &&
                   fprintf(out, "  %s t0, t0, t1\n", plan.opcode) >= 0 &&
                   rv32_canon(out, f, ins->type, "t0") &&
                   rv32_slot(out, fr, ins->result, "t0", false);
        if (plan.strategy != MINIC_RISCV_INTEGER_RUNTIME_LIBCALL)
            return false;
        return rv32_load2(out, fr, ins) &&
               fprintf(out, "  mv a0, t0\n  mv a1, t1\n  call %s\n",
                       plan.symbol) >= 0 &&
               rv32_canon(out, f, ins->type, "a0") &&
               rv32_slot(out, fr, ins->result, "a0", false);
    }
    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:
    case MINIC_CORE_INSTRUCTION_SCALAR_BITCAST: {
        const MinicCoreValueId id = ins->value.operand;
        unsigned from_words, to_words;
        if (id >= f->value_count ||
            !rv32_scalar_storage_words(f, f->values[id].type, &from_words) ||
            !rv32_scalar_storage_words(f, ins->type, &to_words))
            return false;
        if (from_words == 2U) {
            if (!rv32_pair_slot(out, fr, id, "t0", "t1", true)) return false;
        } else if (!rv32_slot(out, fr, id, "t0", true)) return false;
        if (to_words == 2U) {
            if (from_words == 1U) {
                if (ins->kind == MINIC_CORE_INSTRUCTION_SCALAR_BITCAST ||
                    !rv32_scalar(f, f->values[id].type,
                                 &bits, &is_signed) ||
                    fprintf(out, is_signed ? "  srai t1, t0, 31\n" :
                                          "  li t1, 0\n") < 0)
                    return false;
            }
            return rv32_pair_slot(out, fr, ins->result,
                                  "t0", "t1", false);
        }
        return rv32_canon(out, f, ins->type, "t0") &&
               rv32_slot(out, fr, ins->result, "t0", false);
    }
    /* RV32I provides no Zbb CLZ/CTZ. Preserve the target-neutral
     * Core operation using strictly base-I integer instructions, with
     * exactly the same 8/16/32-bit semantics as the RV64 O0 lowering.
     * Two-XLEN operations still fail closed until separately qualified. */
    case MINIC_CORE_INSTRUCTION_INTEGER_CLZ:
    case MINIC_CORE_INSTRUCTION_INTEGER_CTZ: {
        MinicCoreValueId input = ins->value.operand;
        unsigned width;
        bool signed_input;
        bool wide;
        if (input >= f->value_count ||
            !minic_type_is_unsigned_integer(f->values[input].type) ||
            !minic_type_equal(ins->type, minic_type_int()))
            return false;
        wide = rv32_is_wide_integer(f, f->values[input].type);
        if (wide) {
            /* Unsigned long long is two base-I XLEN words. Select the
             * nonzero half, then reuse the 32-bit count loop verbatim.
             * t4 owns the 0/32 half-offset; t0 the selected operand.
             * Neither M nor Zbb instructions are required. */
            if (!rv32_pair_slot(out, fr, input, "t0", "t3", true))
                return false;
            width = 32U;
            if (ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_CLZ) {
                if (fprintf(out,
                    "  li t4, 0\n"
                    "  bnez t3, .L%s_rv32_clz64_high_%" PRIu32 "\n"
                    "  li t4, 32\n"
                    "  j .L%s_rv32_clz64_start_%" PRIu32 "\n"
                    ".L%s_rv32_clz64_high_%" PRIu32 ":\n"
                    "  mv t0, t3\n"
                    ".L%s_rv32_clz64_start_%" PRIu32 ":\n",
                    symbol, ins->result, symbol, ins->result,
                    symbol, ins->result, symbol, ins->result) < 0)
                    return false;
            } else {
                if (fprintf(out,
                    "  li t4, 0\n"
                    "  bnez t0, .L%s_rv32_ctz64_start_%" PRIu32 "\n"
                    "  li t4, 32\n"
                    "  mv t0, t3\n"
                    ".L%s_rv32_ctz64_start_%" PRIu32 ":\n",
                    symbol, ins->result, symbol, ins->result) < 0)
                    return false;
            }
        } else if (!rv32_scalar(f, f->values[input].type, &width, &signed_input) ||
                   signed_input || width < 8U || width > 32U ||
                   !rv32_slot(out, fr, input, "t0", true)) {
            return false;
        }
        if (!minic_riscv_emit_word_bitscan(
                out, ins->kind, symbol, "rv32",
                ins->result, width, 32U))
            return false;
        /* High- or low-half zero adds exactly one XLEN to the result. */
        return (!wide || fprintf(out, "  add t1, t1, t4\n") >= 0) &&
               rv32_slot(out, fr, ins->result, "t1", false);
    }
    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:
    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:
        return rv32_slot(out, fr, ins->value.operand, "t0", true) &&
               fprintf(out, ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_NEGATE ?
                       "  neg t0, t0\n" :
                       ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT ?
                       "  not t0, t0\n" : "  seqz t0, t0\n") >= 0 &&
               rv32_canon(out, f, ins->type, "t0") &&
               rv32_slot(out, fr, ins->result, "t0", false);
    default:
        return false;
    }
    return op != NULL && rv32_load2(out, fr, ins) &&
           fprintf(out, "  %s t0, t0, t1\n", op) >= 0 &&
           rv32_canon(out, f, ins->type, "t0") &&
           rv32_slot(out, fr, ins->result, "t0", false);
}
