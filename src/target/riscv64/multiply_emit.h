#ifndef MINIC_TARGET_RISCV64_MULTIPLY_EMIT_H
#define MINIC_TARGET_RISCV64_MULTIPLY_EMIT_H
#include "target/riscv64/isa.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/* One RV64 multiplication machine implementation, selected by ISA features.
 * lhs/rhs are caller-owned scratch operands and may be clobbered by software
 * lowering. accumulator and test must not alias either input register. */
bool minic_riscv64_emit_multiply64(
    FILE *file, const MinicRiscvIsa *isa,
    const char *result, const char *lhs, const char *rhs,
    const char *accumulator, const char *test);

bool minic_riscv64_power_of_two(size_t value);
unsigned minic_riscv64_log2_power_of_two(size_t value);
#endif
