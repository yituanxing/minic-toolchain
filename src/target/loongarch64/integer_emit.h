#ifndef MINIC_TARGET_LOONGARCH64_INTEGER_EMIT_H
#define MINIC_TARGET_LOONGARCH64_INTEGER_EMIT_H

#include "core/core_ir.h"
#include "frontend/ast.h"
#include "target/target_info.h"
#include "target/common/lp64_frame.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* The same machine-immediate admission used by native emission and the
 * Common value-liveness pass. Only proven scalar RHS constants qualify. */
bool minic_loongarch64_integer_rhs_immediate(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    MinicCoreValueId *rhs_id, int64_t *encoded_immediate);

/* Native Core ALU emission with one shared semantic classifier.
 * GP scalar results request the standard SSA slot writeback. */
bool minic_loongarch64_emit_integer_instruction(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    const MinicLP64FramePlan *frame_plan,
    bool dynamic_frame, bool lhs_in_previous_gp,
    bool *needs_gp_store);

#endif
