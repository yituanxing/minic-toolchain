#ifndef MINIC_TARGET_RISCV64_DIVIDE_EMIT_H
#define MINIC_TARGET_RISCV64_DIVIDE_EMIT_H

#include "target/riscv64/isa.h"
#include <stdbool.h>
#include <stdio.h>

/* t0 / t1 are the normalized dividend/divisor. Emit the selected ISA
 * implementation and return the quotient or remainder in t0. Scratch t2-t6
 * is caller-clobbered by the RV64 frame-based backend. This is machine
 * lowering; C integer width promotion and result conversion stay in Core. */
bool minic_riscv64_emit_divrem64(FILE *file, const MinicRiscvIsa *isa,
                                 bool is_unsigned, bool remainder);
#endif
