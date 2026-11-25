#include "core/value_uses.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    MinicCoreFunction fn = {0};
    MinicCoreBlock block = {0};
    MinicCoreInstruction ins[4] = {0};
    MinicCoreInstructionId ids[4] = {0U, 1U, 2U, 3U};
    MinicCoreCallArgument call_args[1] = {{0}};
    size_t uses[4] = {99U, 99U, 99U, 99U};
    fn.values = NULL;
    fn.value_count = 4U;
    fn.instructions = ins;
    fn.instruction_count = 4U;
    fn.blocks = &block;
    fn.block_count = 1U;
    block.instructions = ids;
    block.instruction_count = 4U;
    block.has_terminator = true;
    block.terminator.kind = MINIC_CORE_TERMINATOR_RETURN;
    block.terminator.return_value = 2U;
    ins[0].kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
    ins[0].result = 0U;
    ins[1].kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
    ins[1].result = 1U;
    ins[2].kind = MINIC_CORE_INSTRUCTION_INTEGER_ADD;
    ins[2].result = 2U;
    ins[2].value.binary.left = 0U;
    ins[2].value.binary.right = 0U;
    ins[3].kind = MINIC_CORE_INSTRUCTION_STORE;
    ins[3].result = MINIC_CORE_VALUE_INVALID;
    ins[3].value.store.address = 1U;
    ins[3].value.store.stored_value = 2U;

    if (!minic_core_function_value_use_counts(&fn, uses, 4U) ||
        uses[0] != 2U || uses[1] != 1U ||
        uses[2] != 2U || uses[3] != 0U) {
        fputs("CORE_VALUE_USE_COUNTS=FAIL duplicate/terminator/dead\n", stderr);
        return 1;
    }
    /* Repeated invocation must reset the once-owned table, not add another
     * occurrence to a previous run's liveness facts. */
    if (!minic_core_function_value_use_counts(&fn, uses, 4U) ||
        uses[0] != 2U || uses[3] != 0U) return 2;
    if (minic_core_function_value_use_counts(&fn, uses, 3U)) return 3;
    ids[3] = 99U;
    if (minic_core_function_value_use_counts(&fn, uses, 4U)) return 4;
    ids[3] = 3U;
    ins[2].value.binary.left = 99U;
    if (minic_core_function_value_use_counts(&fn, uses, 4U)) return 5;
    ins[2].value.binary.left = 0U;
    block.has_terminator = false;
    if (minic_core_function_value_use_counts(&fn, uses, 4U)) return 6;

    /* A corrupt call-argument extent must fail closed without dereferencing
     * absent storage; valid direct/indirect calls still visit each use. */
    block.has_terminator = true;
    block.terminator.kind = MINIC_CORE_TERMINATOR_RETURN;
    block.terminator.return_value = MINIC_CORE_VALUE_INVALID;
    block.instruction_count = 1U;
    ids[0] = 2U;
    ins[2].kind = MINIC_CORE_INSTRUCTION_CALL;
    ins[2].value.call.argument_begin = 0U;
    ins[2].value.call.argument_count = 1U;
    fn.call_argument_count = 1U;
    fn.call_arguments = NULL;
    if (minic_core_function_value_use_counts(&fn, uses, 4U)) return 8;
    call_args[0].kind = MINIC_CORE_CALL_ARGUMENT_VALUE;
    call_args[0].value.value_id = 0U;
    fn.call_arguments = call_args;
    if (!minic_core_function_value_use_counts(&fn, uses, 4U) ||
        uses[0] != 1U || uses[1] != 0U) return 9;
    ins[2].kind = MINIC_CORE_INSTRUCTION_INDIRECT_CALL;
    ins[2].value.indirect_call.callee = 1U;
    ins[2].value.indirect_call.argument_begin = 0U;
    ins[2].value.indirect_call.argument_count = 1U;
    fn.call_arguments = NULL;
    if (minic_core_function_value_use_counts(&fn, uses, 4U)) return 10;
    fn.call_arguments = call_args;
    if (!minic_core_function_value_use_counts(&fn, uses, 4U) ||
        uses[0] != 1U || uses[1] != 1U) return 11;
    (void)memset(&fn, 0, sizeof(fn));
    if (!minic_core_function_value_use_counts(&fn, NULL, 0U)) return 7;
    puts("CORE_VALUE_USE_COUNTS=PASS duplicate_operand=2 terminator=1 dead_result=0 malformed_fail_closed=1 call_table=1");
    return 0;
}
