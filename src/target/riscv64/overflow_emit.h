#ifndef MINIC_TARGET_RISCV64_OVERFLOW_EMIT_H
#define MINIC_TARGET_RISCV64_OVERFLOW_EMIT_H
#include "target/riscv64/frame_layout.h"
#include "core/core_ir.h"
#include "frontend/ast.h"
#include <stdbool.h>
#include <stdio.h>

/* Overflow has its own signed/unsigned width and mixed-type scratch
 * legalization, unrelated to ordinary scalar arithmetic emission. */
bool minic_riscv64_emit_overflow_instruction(
    FILE *, const MinicC0Program *, const MinicCoreFunction *,
    const MinicRiscv64CoreFrame *, const MinicCoreInstruction *);
#endif
