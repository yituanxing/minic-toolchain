#ifndef MINIC_TARGET_LOONGARCH64_FP_EMIT_H
#define MINIC_TARGET_LOONGARCH64_FP_EMIT_H

#include "core/core_ir.h"
#include "frontend/ast.h"
#include "target/common/lp64_frame.h"
#include "target/target_info.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/* Core FP instruction family; native opcodes and ABI scratch registers stay
 * ISA-owned. needs_gp_store requests the existing integer result writeback. */
bool minic_loongarch64_emit_fp_instruction(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    size_t offset, size_t id, bool dynamic_frame, const MinicLP64FramePlan *frame_plan,
    bool *needs_gp_store);

#endif
