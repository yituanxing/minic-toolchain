#include "core/core_ir.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    MinicCoreFunction fn = {0};
    MinicCoreInstruction ins[1] = {0};
    MinicCoreValue values[1] = {0};
    MinicCoreBlock blocks[3] = {0};
    MinicCoreTypeContext types;
    MinicCoreType type = {0};
    MinicCoreTypeId int_id, double_id;
    bool reachable[3] = {false, false, false};
    size_t i;

    minic_core_type_context_initialize(&types);
    type.kind = MINIC_CORE_TYPE_INTEGER;
    type.has_layout = true;
    type.size = 4U;
    type.alignment = 4U;
    type.value.integer.bit_width = 32U;
    type.value.integer.is_signed = true;
    if (!minic_core_type_register(&types, &type, &int_id)) return 10;
    (void)memset(&type, 0, sizeof(type));
    type.kind = MINIC_CORE_TYPE_FLOAT;
    type.has_layout = true;
    type.size = 8U;
    type.alignment = 8U;
    type.value.floating.format = MINIC_CORE_FLOAT_BINARY64;
    if (!minic_core_type_register(&types, &type, &double_id)) return 11;

    fn.entry_block = 0U;
    fn.type_context = &types;
    fn.values = values;
    fn.value_count = 1U;
    fn.instructions = ins;
    fn.instruction_count = 1U;
    fn.blocks = blocks;
    fn.block_count = 3U;
    values[0].core_type_id = int_id;
    values[0].definition = 0U;
    ins[0].kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
    ins[0].result = 0U;
    ins[0].core_type_id = int_id;
    ins[0].value.integer_value = 1;
    blocks[0].has_terminator = true;
    blocks[0].terminator.kind = MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH;
    blocks[0].terminator.conditional.condition = 0U;
    blocks[0].terminator.conditional.when_true = 1U;
    blocks[0].terminator.conditional.when_false = 2U;
    for (i = 1U; i < 3U; ++i) {
        blocks[i].has_terminator = true;
        blocks[i].terminator.kind = MINIC_CORE_TERMINATOR_RETURN;
        blocks[i].terminator.return_value = MINIC_CORE_VALUE_INVALID;
    }
    for (i = 0U; i < 3U; ++i)
        blocks[i].source_label_id = SIZE_MAX;
    if (minic_core_function_fold_literal_branches(&fn) != 1U ||
        blocks[0].terminator.kind != MINIC_CORE_TERMINATOR_BRANCH ||
        blocks[0].terminator.branch_target != 1U ||
        !minic_core_function_mark_reachable_blocks(&fn, reachable, 3U) ||
        !reachable[0] || !reachable[1] || reachable[2]) return 1;

    /* False literal selects the other successor without changing effects. */
    blocks[0].terminator.kind = MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH;
    blocks[0].terminator.conditional.condition = 0U;
    blocks[0].terminator.conditional.when_true = 1U;
    blocks[0].terminator.conditional.when_false = 2U;
    ins[0].value.integer_value = 0;
    if (minic_core_function_fold_literal_branches(&fn) != 1U ||
        blocks[0].terminator.branch_target != 2U) return 2;

    /* A runtime parameter is not a literal, even when its value happens to
     * be one at some callsite. Every edge must remain reachable. */
    blocks[0].terminator.kind = MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH;
    blocks[0].terminator.conditional.condition = 0U;
    blocks[0].terminator.conditional.when_true = 1U;
    blocks[0].terminator.conditional.when_false = 2U;
    ins[0].kind = MINIC_CORE_INSTRUCTION_PARAMETER;
    if (minic_core_function_fold_literal_branches(&fn) != 0U ||
        blocks[0].terminator.kind != MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH)
        return 3;

    ins[0].kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
    values[0].definition = 99U;
    if (minic_core_function_fold_literal_branches(&fn) != 0U ||
        blocks[0].terminator.kind != MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH)
        return 4;
    values[0].definition = 0U;
    blocks[0].terminator.conditional.condition = 99U;
    if (minic_core_function_fold_literal_branches(&fn) != 0U ||
        blocks[0].terminator.kind != MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH)
        return 5;
    blocks[0].terminator.conditional.condition = 0U;
    blocks[0].terminator.conditional.when_true = 99U;
    ins[0].value.integer_value = 1;
    if (minic_core_function_fold_literal_branches(&fn) != 0U ||
        blocks[0].terminator.kind != MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH)
        return 6;
    blocks[0].terminator.conditional.when_true = 1U;
    blocks[0].terminator.conditional.when_false = 99U;
    if (minic_core_function_fold_literal_branches(&fn) != 0U ||
        blocks[0].terminator.kind != MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH)
        return 7;
    blocks[0].terminator.conditional.when_false = 2U;
    values[0].core_type_id = double_id;
    ins[0].core_type_id = double_id;
    if (minic_core_function_fold_literal_branches(&fn) != 0U ||
        blocks[0].terminator.kind != MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH)
        return 8;
    (void)memset(&fn, 0, sizeof(fn));
    if (minic_core_function_fold_literal_branches(&fn) != 0U) return 9;
    minic_core_type_context_destroy(&types);
    puts("CORE_LITERAL_BRANCH_FOLD=PASS true=1 false=1 dynamic_preserved=1 unreachable_pruned=1 malformed_fail_closed=6");
    return 0;
}
