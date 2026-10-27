#include "target/common/core_function_facts.h"
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    MinicCoreInstruction instructions[6] = {
        {.kind = MINIC_CORE_INSTRUCTION_CALL,
         .value.call = {.argument_count = 9U}},
        {.kind = MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS, .result = 2U},
        {.kind = MINIC_CORE_INSTRUCTION_DYNAMIC_STACK_ALLOC},
        {.kind = MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS, .result = 7U},
        {.kind = MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM},
        {.kind = MINIC_CORE_INSTRUCTION_INDIRECT_CALL,
         .value.indirect_call = {.argument_count = 4U}},
    };
    MinicCoreFunction fn = {0};
    MinicCoreFunctionFacts facts = {0};
    MinicCoreValueId *results = NULL;
    fn.instructions = instructions;
    fn.instruction_count = 6U;
    if (!minic_core_function_facts_analyze_collect(&fn, &facts, &results))
        return 1;
    if (facts.direct_call_count != 1U || facts.indirect_call_count != 1U ||
        facts.max_call_arity != 9U || facts.variadic_address_count != 2U ||
        !facts.needs_saved_return_address ||
        !facts.uses_dynamic_stack_alloc || !facts.uses_structured_inline_asm ||
        facts.variadic_address_results != results || results == NULL ||
        results[0] != 2U || results[1] != 7U)
        return 2;
    free(results);
    results = NULL;
    if (!minic_core_function_facts_analyze(&fn, &facts))
        return 3;
    if (facts.variadic_address_count != 2U ||
        facts.variadic_address_results != NULL ||
        facts.direct_call_count != 1U || facts.indirect_call_count != 1U)
        return 4;
    if (!minic_core_function_facts_analyze_collect(&fn, &facts, &results))
        return 5;
    /* The caller must not accidentally overwrite an allocated index. */
    if (minic_core_function_facts_analyze_collect(&fn, &facts, &results))
        return 6;
    free(results);
    puts("MINIC_COMMON_FACTS_SINGLE_PASS=PASS owned_va=2 borrowed_standalone=0");
    return 0;
}
