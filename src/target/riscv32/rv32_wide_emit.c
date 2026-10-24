#include "target/riscv32/rv32_internal.h"
#include "target/riscv/instruction_select.h"
#include <inttypes.h>
#include <stdint.h>

/* RV32I 2-XLEN integer machine lowering. One C long long is one Core
 * semantic scalar with low/high words in ILP32 frame and a0/a1 ABI.
 * No M/Z-extension instructions are synthesized here. */
bool rv32_is_wide_integer(const MinicCoreFunction *f, MinicType type) {
    MinicType effective = type;
    if (minic_type_is_enum(type) &&
        !minic_core_function_effective_integer_type(f, type, &effective))
        return false;
    return minic_type_is_integer(effective) &&
           effective.integer_rank == MINIC_INTEGER_RANK_LONG_LONG;
}

static bool wide_pair_operands(FILE *out, const MinicCoreFunction *f,
                               const Rv32Frame *fr,
                               const MinicCoreInstruction *ins) {
    MinicCoreValueId left = ins->value.binary.left;
    MinicCoreValueId right = ins->value.binary.right;
    return left < f->value_count && right < f->value_count &&
           rv32_is_wide_integer(f, f->values[left].type) &&
           rv32_is_wide_integer(f, f->values[right].type) &&
           rv32_pair_slot(out, fr, left, "t0", "t1", true) &&
           rv32_pair_slot(out, fr, right, "t2", "t3", true);
}

/* C permits any integer type for the shift count. A double-word count
 * is not yet qualified; narrow counts are used without truncation. */
static bool wide_shift(FILE *out, const MinicCoreFunction *f,
                       const Rv32Frame *fr, const MinicCoreInstruction *ins,
                       const char *symbol) {
    const MinicCoreValueId left = ins->value.binary.left;
    const MinicCoreValueId right = ins->value.binary.right;
    bool right_shift = ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT;
    bool signed_shift;
    unsigned shift_words;
    if (symbol == NULL || left >= f->value_count ||
        right >= f->value_count ||
        !rv32_is_wide_integer(f, f->values[left].type) ||
        !minic_type_is_integer(f->values[right].type) ||
        !rv32_scalar_storage_words(f, f->values[right].type, &shift_words))
        return false;
    signed_shift = right_shift &&
                   !minic_type_is_unsigned_integer(f->values[left].type);
    if (!rv32_pair_slot(out, fr, left, "t0", "t1", true))
        return false;
    if (shift_words == 2U) {
        if (!rv32_pair_slot(out, fr, right, "t2", "t5", true) ||
            fprintf(out, "  bnez t5, .L%s_wide_ge64_%" PRIu32 "\n",
                    symbol, ins->result) < 0) return false;
    } else if (!rv32_slot(out, fr, right, "t2", true)) {
        return false;
    }
    /* Branch labels include the function symbol and unique SSA result.
     * RV32I register shifts mask to 5 bits: handle 0/32/64 boundaries
     * explicitly rather than accidentally turning shift-32 into shift-0. */
    if (fprintf(out,
        "  beqz t2, .L%s_wide_done_%" PRIu32 "\n"
        "  li t3, 64\n"
        "  bgeu t2, t3, .L%s_wide_ge64_%" PRIu32 "\n"
        "  li t3, 32\n"
        "  bgeu t2, t3, .L%s_wide_ge32_%" PRIu32 "\n"
        "  sub t3, t3, t2\n",
        symbol, ins->result, symbol, ins->result,
        symbol, ins->result) < 0)
        return false;
    if (!right_shift) {
        if (fprintf(out,
             "  srl t4, t0, t3\n"
             "  sll t1, t1, t2\n"
             "  or t1, t1, t4\n"
             "  sll t0, t0, t2\n") < 0)
            return false;
    } else if (fprintf(out,
             "  sll t4, t1, t3\n"
             "  srl t0, t0, t2\n"
             "  or t0, t0, t4\n"
             "  %s t1, t1, t2\n",
             signed_shift ? "sra" : "srl") < 0)
        return false;
    if (fprintf(out,
        "  j .L%s_wide_done_%" PRIu32 "\n"
        ".L%s_wide_ge32_%" PRIu32 ":\n"
        "  addi t2, t2, -32\n",
        symbol, ins->result, symbol, ins->result) < 0)
        return false;
    if (!right_shift) {
        if (fprintf(out,
            "  sll t1, t0, t2\n"
            "  li t0, 0\n") < 0) return false;
    } else if (fprintf(out,
            "  %s t0, t1, t2\n"
            "  %s\n",
            signed_shift ? "sra" : "srl",
            signed_shift ? "srai t1, t1, 31" : "li t1, 0") < 0)
        return false;
    if (fprintf(out,
        "  j .L%s_wide_done_%" PRIu32 "\n"
        ".L%s_wide_ge64_%" PRIu32 ":\n",
        symbol, ins->result, symbol, ins->result) < 0)
        return false;
    if (signed_shift) {
        if (fprintf(out,
            "  srai t1, t1, 31\n"
            "  mv t0, t1\n") < 0) return false;
    } else if (fprintf(out, "  li t0, 0\n  li t1, 0\n") < 0)
        return false;
    if (fprintf(out, ".L%s_wide_done_%" PRIu32 ":\n",
                symbol, ins->result) < 0)
        return false;
    return rv32_pair_slot(out, fr, ins->result, "t0", "t1", false);
}

bool rv32_emit_wide_integer(FILE *out, const MinicCoreFunction *f,
                            const Rv32Frame *fr,
                            const MinicCoreInstruction *ins,
                            const char *symbol) {
    const MinicCoreInstructionKind kind = ins->kind;
    if (out == NULL || f == NULL || fr == NULL || ins == NULL)
        return false;
    switch (kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
    case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:
        if (!rv32_is_wide_integer(f, ins->type) ||
            !wide_pair_operands(out, f, fr, ins)) return false;
        if (kind == MINIC_CORE_INSTRUCTION_INTEGER_ADD) {
            if (fprintf(out,
                "  add t4, t0, t2\n"
                "  sltu t5, t4, t0\n"
                "  add t1, t1, t3\n"
                "  add t1, t1, t5\n"
                "  mv t0, t4\n") < 0) return false;
        } else if (kind == MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT) {
            if (fprintf(out,
                "  sltu t5, t0, t2\n"
                "  sub t0, t0, t2\n"
                "  sub t1, t1, t3\n"
                "  sub t1, t1, t5\n") < 0) return false;
        } else {
            const char *op = kind == MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND ?
                             "and" : kind == MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR ?
                             "or" : "xor";
            if (fprintf(out,
                "  %s t0, t0, t2\n"
                "  %s t1, t1, t3\n", op, op) < 0)
                return false;
        }
        return rv32_pair_slot(out, fr, ins->result, "t0", "t1", false);
    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT: {
        const MinicCoreValueId id = ins->value.operand;
        if (!rv32_is_wide_integer(f, ins->type) ||
            id >= f->value_count ||
            !rv32_is_wide_integer(f, f->values[id].type) ||
            !rv32_pair_slot(out, fr, id, "t0", "t1", true))
            return false;
        if (kind == MINIC_CORE_INSTRUCTION_INTEGER_NEGATE) {
            if (fprintf(out,
                "  snez t2, t0\n"
                "  neg t0, t0\n"
                "  neg t1, t1\n"
                "  sub t1, t1, t2\n") < 0)
                return false;
        } else if (fprintf(out,
                           "  not t0, t0\n"
                           "  not t1, t1\n") < 0)
            return false;
        return rv32_pair_slot(out, fr, ins->result, "t0", "t1", false);
    }
    case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE:
    case MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER: {
        /* Pinned original LLVM compiler-rt DI arithmetic. We select the
         * standard symbol and implement only the psABI register transfer. */
        const char *helper = minic_riscv_integer_arithmetic_symbol(
            kind, minic_type_is_unsigned_integer(ins->type), 64U);
        if (helper == NULL || !rv32_is_wide_integer(f, ins->type) ||
            !wide_pair_operands(out, f, fr, ins))
            return false;
        return fprintf(out,
                 "  mv a0, t0\n  mv a1, t1\n"
                 "  mv a2, t2\n  mv a3, t3\n"
                 "  call %s\n", helper) >= 0 &&
               rv32_pair_slot(out, fr, ins->result, "a0", "a1", false);
    }
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT:
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT:
        return rv32_is_wide_integer(f, ins->type) &&
               wide_shift(out, f, fr, ins, symbol);
    case MINIC_CORE_INSTRUCTION_INTEGER_LESS: {
        bool is_unsigned;
        if (!wide_pair_operands(out, f, fr, ins)) return false;
        is_unsigned = minic_type_is_unsigned_integer(
            f->values[ins->value.binary.left].type);
        if (fprintf(out,
                "  xor t4, t1, t3\n"
                "  seqz t4, t4\n"
                "  sltu t5, t0, t2\n"
                "  and t5, t5, t4\n"
                "  %s t0, t1, t3\n"
                "  or t0, t0, t5\n",
                is_unsigned ? "sltu" : "slt") < 0)
            return false;
        return rv32_slot(out, fr, ins->result, "t0", false);
    }
    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:
        return wide_pair_operands(out, f, fr, ins) &&
               fprintf(out,
                 "  xor t0, t0, t2\n"
                 "  xor t1, t1, t3\n"
                 "  or t0, t0, t1\n"
                 "  seqz t0, t0\n") >= 0 &&
               rv32_slot(out, fr, ins->result, "t0", false);
    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO: {
        const MinicCoreValueId id = ins->value.operand;
        return id < f->value_count &&
               rv32_is_wide_integer(f, f->values[id].type) &&
               rv32_pair_slot(out, fr, id, "t0", "t1", true) &&
               fprintf(out, "  or t0, t0, t1\n  seqz t0, t0\n") >= 0 &&
               rv32_slot(out, fr, ins->result, "t0", false);
    }
    default: return false;
    }
}
