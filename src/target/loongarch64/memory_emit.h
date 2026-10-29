#ifndef MINIC_TARGET_LOONGARCH64_MEMORY_EMIT_H
#define MINIC_TARGET_LOONGARCH64_MEMORY_EMIT_H
#include "core/core_ir.h"
#include "frontend/ast.h"
#include "target/common/lp64_frame.h"
#include "target/target_info.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/* Native Core memory loads/stores and relaxed atomic byte transfers. */
bool minic_loongarch64_emit_memory_instruction(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    const MinicLP64FramePlan *frame_plan,
    bool dynamic_frame, bool *needs_gp_store);

/* Store a compiler-materialized structured ASM register output through the
 * same alignment proof and native/byte-fallback transport as ordinary Core
 * stores. The chosen scratch register must not alias value or address. */
bool minic_loongarch64_emit_asm_output_store(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, MinicCoreValueId address,
    size_t width, const char *value_reg, const char *address_reg,
    const char *scratch_reg);
#endif
