#ifndef MINIC_TARGET_RISCV64_FP_EMIT_H
#define MINIC_TARGET_RISCV64_FP_EMIT_H
#include "target/riscv64/frame_layout.h"
#include "target/riscv64/isa.h"
#include "core/core_ir.h"
#include "frontend/ast.h"
#include <stdbool.h>
#include <stdio.h>

/* RV64 native F/D instruction family and conversions, separately owned from
 * the integer machinery. Core instruction family is architecture-neutral,
 * but the register sequence/rounding and psABI are RV64-specific. */
bool minic_riscv64_emit_fp_instruction(
    FILE *, const MinicC0Program *, const MinicCoreFunction *,
    const MinicRiscv64CoreFrame *, const MinicCoreInstruction *,
    const MinicRiscvIsa *);
#endif
