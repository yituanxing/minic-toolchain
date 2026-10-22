#ifndef MINIC_TARGET_RISCV64_INLINE_ASM_EMIT_H
#define MINIC_TARGET_RISCV64_INLINE_ASM_EMIT_H
#include "frontend/ast.h"
#include "core/core_ir.h"
#include "target/riscv64/frame_layout.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/* Six existing Core asm families: validated and emitted by the same module. */
const MinicCoreFixedRegisterBinding *minic_riscv64_inline_asm_fixed_register_binding(
    const MinicCoreFunction *, size_t);
bool minic_riscv64_inline_asm_instruction_supported(
    const MinicCoreFunction *, const MinicCoreInstruction *);
bool minic_riscv64_emit_inline_asm_instruction(
    FILE *, const MinicC0Program *, const MinicCoreFunction *,
    const MinicRiscv64CoreFrame *, const char *, const MinicCoreInstruction *, size_t);
#endif
