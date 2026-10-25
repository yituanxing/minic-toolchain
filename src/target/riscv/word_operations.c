#include "target/riscv/word_operations.h"

#include <inttypes.h>

bool minic_riscv_emit_word_pair_binary(FILE *out, MinicCoreInstructionKind kind) {
    const char *opcode;
    if (out == NULL) return false;
    switch (kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
        return fprintf(out,
            "  add t4, t0, t2\n"
            "  sltu t5, t4, t0\n"
            "  add t1, t1, t3\n"
            "  add t1, t1, t5\n"
            "  mv t0, t4\n") >= 0;
    case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
        return fprintf(out,
            "  sltu t5, t0, t2\n"
            "  sub t0, t0, t2\n"
            "  sub t1, t1, t3\n"
            "  sub t1, t1, t5\n") >= 0;
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND: opcode = "and"; break;
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR: opcode = "or"; break;
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR: opcode = "xor"; break;
    default: return false;
    }
    return fprintf(out, "  %s t0, t0, t2\n  %s t1, t1, t3\n",
                   opcode, opcode) >= 0;
}

bool minic_riscv_emit_word_pair_unary(FILE *out, MinicCoreInstructionKind kind) {
    if (out == NULL) return false;
    switch (kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:
        /* The low word borrows into the high word iff it was nonzero. */
        return fprintf(out,
            "  snez t4, t0\n"
            "  neg t0, t0\n"
            "  neg t1, t1\n"
            "  sub t1, t1, t4\n") >= 0;
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:
        return fprintf(out, "  not t0, t0\n  not t1, t1\n") >= 0;
    default:
        return false;
    }
}

bool minic_riscv_emit_word_bitscan(FILE *out, MinicCoreInstructionKind kind,
                                  const char *symbol, const char *label_scope,
                                  uint32_t result, unsigned width, unsigned xlen) {
    if (out == NULL || symbol == NULL || label_scope == NULL ||
        (xlen != 32U && xlen != 64U) || width == 0U || width > xlen)
        return false;
    if (kind == MINIC_CORE_INSTRUCTION_INTEGER_CLZ) {
        return fprintf(out,
            "  li t1, 0\n"
            "  li t2, 1\n"
            "  slli t2, t2, %u\n"
            ".L%s_%s_clz_loop_%" PRIu32 ":\n"
            "  and t3, t0, t2\n"
            "  bnez t3, .L%s_%s_clz_done_%" PRIu32 "\n"
            "  addi t1, t1, 1\n"
            "  srli t2, t2, 1\n"
            "  bnez t2, .L%s_%s_clz_loop_%" PRIu32 "\n"
            ".L%s_%s_clz_done_%" PRIu32 ":\n",
            width - 1U, symbol, label_scope, result,
            symbol, label_scope, result,
            symbol, label_scope, result,
            symbol, label_scope, result) >= 0;
    }
    if (kind == MINIC_CORE_INSTRUCTION_INTEGER_CTZ) {
        return fprintf(out,
            "  beqz t0, .L%s_%s_ctz_zero_%" PRIu32 "\n"
            "  li t1, 0\n"
            ".L%s_%s_ctz_loop_%" PRIu32 ":\n"
            "  andi t2, t0, 1\n"
            "  bnez t2, .L%s_%s_ctz_done_%" PRIu32 "\n"
            "  addi t1, t1, 1\n"
            "  srli t0, t0, 1\n"
            "  j .L%s_%s_ctz_loop_%" PRIu32 "\n"
            ".L%s_%s_ctz_zero_%" PRIu32 ":\n"
            "  li t1, %u\n"
            ".L%s_%s_ctz_done_%" PRIu32 ":\n",
            symbol, label_scope, result,
            symbol, label_scope, result,
            symbol, label_scope, result,
            symbol, label_scope, result,
            symbol, label_scope, result, width,
            symbol, label_scope, result) >= 0;
    }
    return false;
}
