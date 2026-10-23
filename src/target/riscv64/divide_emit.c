#include "target/riscv64/divide_emit.h"

#include <stdio.h>

/* Restoring long division over 64 input bits. Keep quotient in the shifting
 * dividend register: each iteration consumes its high bit, then sets the
 * low quotient bit. Remainder is t3, and t6 detects the 65th remainder bit.
 * All operations are RV64I, including signed sign-correction, and no helper
 * library or global assembly label counter is required.
 *
 * This matches RISC-V's unsigned divide-by-zero result (quotient all-ones,
 * remainder dividend). C /0 is undefined; callers must not infer support
 * for defined division-by-zero behavior. Normal signed overflow (-2^63/-1)
 * naturally reproduces the hardware's wrapped low 64-bit quotient. */
bool minic_riscv64_emit_divrem64(FILE *file, const MinicRiscvIsa *isa,
                                 bool is_unsigned, bool remainder) {
    const bool hardware =
        isa == NULL || !isa->strict_isa ||
        minic_riscv_isa_has(isa, MINIC_RISCV_FEATURE_M);
    if (file == NULL) return false;
    if (hardware) {
        const char *op = remainder
            ? (is_unsigned ? "remu" : "rem")
            : (is_unsigned ? "divu" : "div");
        return fprintf(file, "  %s t0, t0, t1\n", op) >= 0;
    }

    /* t2 packs two sign bits: bit 1 for dividend/remainder sign and
     * bit 0 for quotient sign. Keep it untouched by the division loop.
     * Negation is modulo 2^64, so INT64_MIN magnitude remains representable
     * as the unsigned high bit. */
    if (!is_unsigned &&
        fprintf(file,
                "  slt t2, t0, zero\n"
                "  slt t5, t1, zero\n"
                "  xor t5, t5, t2\n"
                "  slli t2, t2, 1\n"
                "  or t2, t2, t5\n"
                "  srai t5, t0, 63\n"
                "  xor t0, t0, t5\n"
                "  sub t0, t0, t5\n"
                "  srai t5, t1, 63\n"
                "  xor t1, t1, t5\n"
                "  sub t1, t1, t5\n") < 0)
        return false;

    if (fprintf(file,
                "  li t3, 0\n"
                "  li t4, 64\n"
                "1:\n"
                "  srli t5, t0, 63\n"
                "  slli t0, t0, 1\n"
                "  srli t6, t3, 63\n"
                "  slli t3, t3, 1\n"
                "  or t3, t3, t5\n"
                "  bnez t6, 2f\n"
                "  bltu t3, t1, 3f\n"
                "2:\n"
                "  sub t3, t3, t1\n"
                "  ori t0, t0, 1\n"
                "3:\n"
                "  addi t4, t4, -1\n"
                "  bnez t4, 1b\n") < 0)
        return false;

    if (!is_unsigned &&
        fprintf(file,
                "  andi t5, t2, 1\n"
                "  beqz t5, 4f\n"
                "  neg t0, t0\n"
                "4:\n"
                "  andi t5, t2, 2\n"
                "  beqz t5, 5f\n"
                "  neg t3, t3\n"
                "5:\n") < 0)
        return false;
    return !remainder || fprintf(file, "  mv t0, t3\n") >= 0;
}
