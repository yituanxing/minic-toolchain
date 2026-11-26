#ifndef MINIC_TARGET_COMMON_CORE_BLOCK_WALK_H
#define MINIC_TARGET_COMMON_CORE_BLOCK_WALK_H

#include "core/core_ir.h"
#include <stdbool.h>
#include <stddef.h>

/* Checked Core block instruction view; traversal ownership stays with callers. */
static inline bool minic_core_block_instruction_at(
    const MinicCoreFunction *fn, const MinicCoreBlock *block, size_t ordinal,
    MinicCoreInstructionId *id, const MinicCoreInstruction **instruction) {
    MinicCoreInstructionId value;
    if (fn == NULL || block == NULL || id == NULL || instruction == NULL ||
        ordinal >= block->instruction_count || block->instructions == NULL ||
        fn->instructions == NULL) return false;
    value = block->instructions[ordinal];
    if (value >= fn->instruction_count) return false;
    *id = value;
    *instruction = &fn->instructions[value];
    return true;
}

#endif
