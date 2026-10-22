#ifndef MINIC_TARGET_RISCV64_SCALAR_EMIT_H
#define MINIC_TARGET_RISCV64_SCALAR_EMIT_H

#include "frontend/ast.h"
#include "core/core_ir.h"
#include "target/riscv64/frame_layout.h"
#include <stdbool.h>
#include <stdio.h>

/* RV64 scalar instructions: native integer, floating, width conversions,
 * bit operations and scalar Core values; frame and ABI decisions are inputs. */
bool minic_riscv64_emit_scalar_instruction(
    FILE *file, const MinicC0Program *program,
    const MinicCoreFunction *function, const MinicRiscv64CoreFrame *frame,
    const char *symbol_name, const MinicCoreInstruction *instruction);

#endif
