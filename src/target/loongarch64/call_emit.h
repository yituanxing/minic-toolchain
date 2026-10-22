#ifndef MINIC_TARGET_LOONGARCH64_CALL_EMIT_H
#define MINIC_TARGET_LOONGARCH64_CALL_EMIT_H

#include "target/common/direct_block_context.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/* The ABI-transfer family: caller argument marshaling, return transport,
 * and callee parameter ingress; all register and instruction syntax remains
 * target-owned. needs_gp_store preserves scalar CALL result finalization. */
bool minic_loongarch64_emit_abi_instruction(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, size_t offset,
    bool *needs_gp_store);

#endif
