#ifndef MINIC_TARGET_RISCV64_CALL_EMIT_H
#define MINIC_TARGET_RISCV64_CALL_EMIT_H

#include "frontend/ast.h"
#include "core/core_ir.h"
#include "target/riscv64/frame_layout.h"
#include <stdbool.h>
#include <stdio.h>

/* RV64 psABI machine transport: function entry parameters and both call kinds.
 * ABI location decisions continue to be owned exclusively by abi.c. */
bool minic_riscv64_emit_incoming_parameter(
    FILE *, const MinicC0Program *, const MinicCoreFunction *,
    const MinicRiscv64CoreFrame *, const MinicCoreInstruction *);
bool minic_riscv64_emit_incoming_parameter_object(
    FILE *, const MinicC0Program *, const MinicCoreFunction *,
    const MinicRiscv64CoreFrame *, const MinicCoreInstruction *);
bool minic_riscv64_emit_direct_call(
    FILE *, const MinicC0Program *, const MinicCoreFunction *,
    const MinicRiscv64CoreFrame *, const MinicCoreInstruction *, size_t instruction_id);
bool minic_riscv64_emit_indirect_call(
    FILE *, const MinicC0Program *, const MinicCoreFunction *,
    const MinicRiscv64CoreFrame *, const MinicCoreInstruction *, size_t instruction_id);
#endif
