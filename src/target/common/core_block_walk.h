#ifndef MINIC_TARGET_COMMON_CORE_BLOCK_WALK_H
#define MINIC_TARGET_COMMON_CORE_BLOCK_WALK_H

#include "core/core_ir.h"
#include <stdbool.h>
#include <stddef.h>

/* One ABI/ISA-neutral lexical CFG traversal and checked instruction view.
 * Reachability remains target-controlled: NULL emits every block, while
 * a target-supplied bitmap skips unreachable blocks without reordering.
 * Machine labels, register state and terminators stay with each emitter. */
typedef struct MinicCoreBlockWalk {
    const MinicCoreFunction *function;
    const bool *visible;
    size_t next;
} MinicCoreBlockWalk;

static inline void minic_core_block_walk_begin(
    MinicCoreBlockWalk *walk, const MinicCoreFunction *function,
    const bool *visible) {
    if (walk == NULL) return;
    walk->function = function;
    walk->visible = visible;
    walk->next = 0U;
}

static inline bool minic_core_block_walk_next(
    MinicCoreBlockWalk *walk, size_t *index, const MinicCoreBlock **block) {
    if (walk == NULL || index == NULL || block == NULL ||
        walk->function == NULL ||
        (walk->function->block_count != 0U &&
         walk->function->blocks == NULL)) return false;
    while (walk->next < walk->function->block_count) {
        const size_t i = walk->next++;
        if (walk->visible != NULL && !walk->visible[i]) continue;
        *index = i;
        *block = &walk->function->blocks[i];
        return true;
    }
    return false;
}

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
