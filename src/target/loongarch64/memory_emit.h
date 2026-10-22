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
#endif
