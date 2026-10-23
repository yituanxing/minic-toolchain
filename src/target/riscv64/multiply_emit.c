#include "target/riscv64/multiply_emit.h"
#include <stdio.h>
#include <string.h>

bool minic_riscv64_power_of_two(size_t value) {
    return value != 0U && (value & (value - 1U)) == 0U;
}

unsigned minic_riscv64_log2_power_of_two(size_t value) {
    unsigned bits = 0U;
    while (value > 1U) {
        value >>= 1U;
        ++bits;
    }
    return bits;
}

/* Classic unsigned shift-add modulo 2^64. Signed C multiplication has the
 * same low 64-bit two's-complement result; narrower C results are normalized
 * by the caller. Loop terminates in <=64 iterations even when rhs is -1.
 * GNU local numeric labels are resolved to nearest 1b/2f so this can be
 * emitted repeatedly in a function, without global counters, symbol hashing,
 * runtime helpers or a separate RV64I backend. */
bool minic_riscv64_emit_multiply64(
    FILE *file, const MinicRiscvIsa *isa,
    const char *result, const char *lhs, const char *rhs,
    const char *accumulator, const char *test) {
    if (file == NULL || result == NULL || lhs == NULL || rhs == NULL ||
        accumulator == NULL || test == NULL ||
        strcmp(lhs, rhs) == 0 || strcmp(lhs, accumulator) == 0 ||
        strcmp(rhs, accumulator) == 0 || strcmp(test, accumulator) == 0 ||
        strcmp(test, lhs) == 0 || strcmp(test, rhs) == 0)
        return false;
    if (isa == NULL || !isa->strict_isa ||
        minic_riscv_isa_has(isa, MINIC_RISCV_FEATURE_M))
        return fprintf(file, "  mul %s, %s, %s\n", result, lhs, rhs) >= 0;
    return fprintf(file,
                   "  li %s, 0\n"
                   "1:\n"
                   "  andi %s, %s, 1\n"
                   "  beqz %s, 2f\n"
                   "  add %s, %s, %s\n"
                   "2:\n"
                   "  srli %s, %s, 1\n"
                   "  slli %s, %s, 1\n"
                   "  bnez %s, 1b\n"
                   "  mv %s, %s\n",
                   accumulator, test, rhs, test,
                   accumulator, accumulator, lhs,
                   rhs, rhs, lhs, lhs, rhs, result, accumulator) >= 0;
}
