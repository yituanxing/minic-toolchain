#include "target/common/core_block_walk.h"
#include <stdio.h>

int main(void) {
    MinicCoreInstruction instructions[2] = {{0}, {0}};
    MinicCoreInstructionId ids[] = {0U, 1U};
    MinicCoreBlock blocks[3] = {{0}, {0}, {0}};
    MinicCoreFunction fn = {0};
    MinicCoreBlockWalk walk = {0};
    const MinicCoreBlock *block = NULL;
    const MinicCoreInstruction *ins = NULL;
    MinicCoreInstructionId id = 0U;
    size_t index = 0U, count = 0U;
    bool visible[3] = {true, false, true};

    blocks[0].instructions = ids;
    blocks[0].instruction_count = 2U;
    fn.blocks = blocks;
    fn.block_count = 3U;
    fn.instructions = instructions;
    fn.instruction_count = 2U;

    minic_core_block_walk_begin(&walk, &fn, NULL);
    while (minic_core_block_walk_next(&walk, &index, &block)) {
        if (index != count || block != &blocks[index]) return 1;
        ++count;
    }
    if (count != 3U) return 2;
    minic_core_block_walk_begin(&walk, &fn, visible);
    count = 0U;
    while (minic_core_block_walk_next(&walk, &index, &block)) {
        if (index != 2U * count || block != &blocks[index]) return 3;
        ++count;
    }
    if (count != 2U) return 4;
    if (!minic_core_block_instruction_at(
            &fn, &blocks[0], 1U, &id, &ins) ||
        id != 1U || ins != &instructions[1]) return 5;
    ids[1] = 2U;
    if (minic_core_block_instruction_at(
            &fn, &blocks[0], 1U, &id, &ins)) return 6;
    ids[1] = 1U;
    if (minic_core_block_instruction_at(
            &fn, &blocks[0], 2U, &id, &ins)) return 7;
    puts("MINIC_FIVE_TARGET_CORE_BLOCK_WALK=PASS all=3 reachable=2 invalid=2");
    return 0;
}
