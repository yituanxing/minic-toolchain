#ifndef MINIC_TARGET_RISCV64_MEMORY_EMIT_H
#define MINIC_TARGET_RISCV64_MEMORY_EMIT_H
#include "frontend/ast.h"
#include "core/core_ir.h"
#include "target/riscv64/frame_layout.h"
#include <stdbool.h>
#include <stdio.h>

/* RV64 Core loads/stores, aggregate memory, and pointer field address. */
bool minic_riscv64_emit_memory_instruction(
    FILE *, const MinicC0Program *, const MinicCoreFunction *,
    const MinicRiscv64CoreFrame *, const MinicCoreInstruction *);
#endif
