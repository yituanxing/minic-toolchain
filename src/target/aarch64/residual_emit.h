#ifndef MINIC_TARGET_AARCH64_RESIDUAL_EMIT_H
#define MINIC_TARGET_AARCH64_RESIDUAL_EMIT_H
#include "target/common/direct_block_context.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/* A complete ISA-specific residual machine family. The centralized Core
 * residual plan decides widths/addresses, not machine register sequences.
 * needs_writeback=false indicates that this Core kind writes the result
 * directly (or is effect-only); true leaves GP writeback to block_emit. */
bool minic_aarch64_emit_residual_instruction(
    FILE *, const MinicDirectLP64BlockContext *, MinicCoreInstructionId,
    size_t block_id, size_t result_offset,
    bool (*normalize)(FILE *, size_t, bool), bool *needs_writeback);
#endif
