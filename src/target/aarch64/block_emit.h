#ifndef MINIC_TARGET_AARCH64_BLOCK_EMIT_H
#define MINIC_TARGET_AARCH64_BLOCK_EMIT_H

#include "target/common/direct_call_plan.h"
#include "target/common/direct_block_context.h"
#include "target/aarch64/frame_emit.h"
#include "target/common/lp64_frame.h"
#include "core/core_ir.h"
#include "frontend/ast.h"
#include "target/target_info.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/* Per-ISA Core block emission, not a new IR or universal LP64 ABI. */
bool minic_aarch64_emit_record_copy(FILE *,
    const MinicDirectLP64BlockContext *, const MinicCoreInstruction *);
bool minic_aarch64_emit_parameter_object(FILE *,
    const MinicDirectLP64BlockContext *, const MinicCoreInstruction *);
bool minic_aarch64_emit_terminator(FILE *,
    const MinicDirectLP64BlockContext *, const MinicCoreBlock *,
    size_t block_id);

bool minic_aarch64_emit_lp64_record_chunk_parameter(FILE *out,
    const MinicA64FrameView *view, size_t offset,
    size_t bytes, bool in_reg, size_t gp, size_t incoming_stack_offset);

bool minic_aarch64_emit_core_blocks(FILE *out, const MinicDirectLP64BlockContext *context);

#endif
