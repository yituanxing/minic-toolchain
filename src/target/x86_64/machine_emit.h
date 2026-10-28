#ifndef MINIC_TARGET_X86_64_MACHINE_EMIT_H
#define MINIC_TARGET_X86_64_MACHINE_EMIT_H

#include "target/x86_64/frame.h"
#include "target/target_info.h"
#include "core/core_ir.h"
#include "frontend/ast.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/* Core -> x86 machine emission; no TU, ELF, frame ownership or diagnostic
 * policy. The calling ABI, register moves and opcode selection stay native.
 * This explicit boundary may later take a Machine IR rather than Core. */
bool minic_x86_64_emit_core_instruction(
    FILE *, const MinicC0Program *, const MinicTargetInfo *,
    const MinicCoreFunction *, const MinicX86Frame *,
    const MinicCoreInstruction *, size_t function_id, size_t instruction_id);
bool minic_x86_64_emit_core_terminator(
    FILE *, const MinicC0Program *, const MinicTargetInfo *,
    const MinicX86Frame *, const MinicCoreTerminator *,
    const MinicCoreFunction *, size_t function_id);

#endif
