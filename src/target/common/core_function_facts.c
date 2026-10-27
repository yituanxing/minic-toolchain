#include "target/common/core_function_facts.h"
#include "target/common/core_instruction_family.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* One Core scan both computes semantic facts and, for the prepared visitor,
 * collects va_start results. Standalone consumers remain allocation-free. */
bool minic_core_function_facts_analyze_collect(
    const MinicCoreFunction *function, MinicCoreFunctionFacts *facts,
    MinicCoreValueId **variadic_results) {
    size_t index, capacity = 0U;
    if (function == NULL || facts == NULL ||
        (function->instruction_count != 0U && function->instructions == NULL) ||
        (variadic_results != NULL && *variadic_results != NULL))
        return false;
    memset(facts, 0, sizeof(*facts));
    for (index = 0U; index < function->instruction_count; ++index) {
        const MinicCoreInstruction *instruction = &function->instructions[index];
        if (minic_core_instruction_is_call(instruction->kind)) {
            if (instruction->kind == MINIC_CORE_INSTRUCTION_CALL)
                ++facts->direct_call_count;
            else
                ++facts->indirect_call_count;
            {
                const size_t arity =
                    instruction->kind == MINIC_CORE_INSTRUCTION_CALL
                        ? instruction->value.call.argument_count
                        : instruction->value.indirect_call.argument_count;
                if (arity > facts->max_call_arity)
                    facts->max_call_arity = arity;
            }
            facts->needs_saved_return_address = true;
            continue;
        }
        switch (instruction->kind) {
        case MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS:
            if (instruction->value.call_frame_address.kind ==
                MINIC_CORE_CALL_FRAME_ADDRESS_RETURN)
                facts->needs_saved_return_address = true;
            break;
        case MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM:
            facts->uses_structured_inline_asm = true;
            break;
        case MINIC_CORE_INSTRUCTION_DYNAMIC_STACK_ALLOC:
            facts->uses_dynamic_stack_alloc = true;
            break;
        case MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS:
            if (variadic_results != NULL) {
                const size_t n = facts->variadic_address_count;
                if (n == capacity) {
                    size_t next;
                    MinicCoreValueId *grown;
                    if (capacity > (SIZE_MAX / sizeof(**variadic_results)) / 2U)
                        goto failed;
                    next = capacity == 0U ? 1U : capacity * 2U;
                    grown = realloc(*variadic_results,
                                    next * sizeof(**variadic_results));
                    if (grown == NULL) goto failed;
                    *variadic_results = grown;
                    capacity = next;
                }
                (*variadic_results)[n] = instruction->result;
            }
            ++facts->variadic_address_count;
            break;
        default:
            break;
        }
    }
    if (variadic_results != NULL)
        facts->variadic_address_results = *variadic_results;
    return true;
failed:
    free(*variadic_results);
    *variadic_results = NULL;
    memset(facts, 0, sizeof(*facts));
    return false;
}

bool minic_core_function_facts_analyze(
    const MinicCoreFunction *function, MinicCoreFunctionFacts *facts) {
    return minic_core_function_facts_analyze_collect(function, facts, NULL);
}
