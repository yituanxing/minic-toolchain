#ifndef MINIC_TARGET_LOONGARCH64_RESIDUAL_EMIT_H
#define MINIC_TARGET_LOONGARCH64_RESIDUAL_EMIT_H
#include "target/common/direct_block_context.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/* A complete ISA-specific residual machine family. The centralized Core
 * residual plan decides widths/addresses, not machine register sequences.
 * needs_writeback=false indicates that this Core kind writes the result
 * directly (or is effect-only); true leaves GP writeback to block_emit. */
bool minic_loongarch64_emit_residual_instruction(
    FILE *, const MinicDirectLP64BlockContext *, MinicCoreInstructionId,
    size_t block_id, size_t result_offset,
    bool native_opcode_presence_checked, bool emits_no_native_opcode,
    bool operand_in_previous_gp,
    bool *needs_writeback);

/* Certifies an address-only Core instruction generates zero native machine
 * instructions through the existing direct-frame-fold producer. Common may
 * extend a register-resident result across precisely these instructions. */
bool minic_loongarch64_elided_frame_address_instruction(
    const MinicDirectLP64BlockContext *, const MinicCoreInstruction *);
#endif
