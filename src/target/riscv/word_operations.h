#ifndef MINIC_TARGET_RISCV_WORD_OPERATIONS_H
#define MINIC_TARGET_RISCV_WORD_OPERATIONS_H

#include "core/core_ir.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* One compiled implementation for the RV32 2x32 and RV64 2x64 Base-I
 * register-pair operations. Both callers preload t0/t1 with the left pair,
 * t2/t3 with the right pair, and reserve t4/t5 as scratch. Results remain
 * in t0/t1. ABI placement, frame slots and canonicalization stay target-owned.
 * No M, A, Zbb or target-width-specific instructions are emitted here. */
bool minic_riscv_emit_word_pair_binary(FILE *out, MinicCoreInstructionKind kind);
bool minic_riscv_emit_word_pair_unary(FILE *out, MinicCoreInstructionKind kind);

/* CLZ/CTZ loop for a single machine word. Input t0; output t1; scratch
 * t2/t3. t4 is retained (RV32 64-bit half-selection offset). Both the
 * label namespace and XLEN are explicit. width may be narrower than XLEN. */
bool minic_riscv_emit_word_bitscan(FILE *out, MinicCoreInstructionKind kind,
                                  const char *symbol, const char *label_scope,
                                  uint32_t result, unsigned width, unsigned xlen);
#endif
