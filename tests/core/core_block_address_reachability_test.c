/* MC-073: a typed Core block address is a CFG escape edge even when its
 * destination has no C-source local-label alias. Unexecuted producers do
 * not make otherwise unreachable blocks live. */
#include "core/core_ir.h"

#include <stdio.h>
#include <string.h>

static bool prepare_three_blocks(MinicCoreFunction *fn) {
    MinicCoreBlockId blocks[3];
    size_t i;

    minic_core_function_initialize(fn);
    for (i = 0U; i < 3U; ++i) {
        if (!minic_core_function_add_block(fn, &blocks[i]) || blocks[i] != i)
            return false;
    }
    return fn->entry_block == blocks[0] &&
           fn->blocks[2].local_label_symbol == NULL;
}

static bool finish_three_blocks(MinicCoreFunction *fn) {
    MinicCoreTerminator terminator = {0};
    size_t i;
    terminator.kind = MINIC_CORE_TERMINATOR_RETURN;
    terminator.return_value = MINIC_CORE_VALUE_INVALID;
    terminator.return_object = MINIC_CORE_OBJECT_INVALID;
    for (i = 0U; i < 3U; ++i) {
        if (!minic_core_function_set_terminator(fn, (MinicCoreBlockId)i,
                                                &terminator)) return false;
    }
    return true;
}

static bool add_block_address(MinicCoreFunction *fn, MinicCoreBlockId producer,
                              MinicCoreBlockId target) {
    MinicCoreInstruction instruction = {0};
    MinicCoreValueId value;
    instruction.kind = MINIC_CORE_INSTRUCTION_BLOCK_ADDRESS;
    instruction.value.block_id = target;
    /* Reachability is a pure Core CFG query: type attachment is irrelevant
     * here and is separately checked by the CoreType verifier suite. */
    instruction.core_type_id = MINIC_CORE_TYPE_INVALID;
    return minic_core_function_append_value_instruction(fn, producer,
                                                        &instruction, &value);
}

static bool expected(const bool actual[3], bool b0, bool b1, bool b2) {
    return actual[0] == b0 && actual[1] == b1 && actual[2] == b2;
}

static int run(void) {
    MinicCoreFunction fn;
    bool reachable[3];

    /* Ordinary return terminators have no successor: block2 is dead. */
    if (!prepare_three_blocks(&fn) || !finish_three_blocks(&fn)) return 1;
    if (!minic_core_function_mark_reachable_blocks(&fn, reachable, 3U) ||
        !expected(reachable, true, false, false)) return 2;
    minic_core_function_destroy(&fn);

    /* Even a genuine address producer cannot create roots from dead code. */
    if (!prepare_three_blocks(&fn) || !add_block_address(&fn, 1U, 2U) ||
        !finish_three_blocks(&fn)) return 3;
    if (!minic_core_function_mark_reachable_blocks(&fn, reachable, 3U) ||
        !expected(reachable, true, false, false)) return 4;
    minic_core_function_destroy(&fn);

    /* An address produced in reachable entry must retain destination 2,
     * despite no label alias, and must not resurrect unrelated block 1. */
    if (!prepare_three_blocks(&fn) || !add_block_address(&fn, 0U, 2U) ||
        !finish_three_blocks(&fn)) return 5;
    if (!minic_core_function_mark_reachable_blocks(&fn, reachable, 3U) ||
        !expected(reachable, true, false, true)) return 6;

    /* A forged block target is rejected, never indexed or silently pruned. */
    fn.instructions[0].value.block_id = 99U;
    if (minic_core_function_mark_reachable_blocks(&fn, reachable, 3U)) return 7;
    fn.instructions[0].value.block_id = 2U;
    if (minic_core_function_mark_reachable_blocks(&fn, reachable, 2U)) return 8;
    minic_core_function_destroy(&fn);
    return 0;
}

int main(void) {
    const int rc = run();
    if (rc != 0) {
        (void)fprintf(stderr, "MC073_CORE_BLOCK_ADDRESS=FAIL case=%d\n", rc);
        return rc;
    }
    (void)puts("MC073_CORE_BLOCK_ADDRESS=PASS escaped=reachable unreferenced=pruned dead_producer=pruned malformed_target=reject capacity=reject");
    return 0;
}
