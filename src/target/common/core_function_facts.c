#include "target/common/core_function_facts.h"
#include <string.h>

/* All five backends consume the same once-per-definition semantic scan
 * through program_visit_prepared_functions. Frame/call/register policies
 * remain target-owned. Standalone compatibility callers may scan directly. */
bool minic_core_function_facts_analyze(
    const MinicCoreFunction *function, MinicCoreFunctionFacts *facts) {
    size_t index;
    if (function == NULL || facts == NULL ||
        (function->instruction_count != 0U && function->instructions == NULL))
        return false;
    memset(facts, 0, sizeof(*facts));
    for (index = 0U; index < function->instruction_count; ++index) {
        const MinicCoreInstruction *instruction = &function->instructions[index];
        switch (instruction->kind) {
        case MINIC_CORE_INSTRUCTION_CALL:
            ++facts->direct_call_count;
            facts->needs_saved_return_address = true;
            break;
        case MINIC_CORE_INSTRUCTION_INDIRECT_CALL:
            ++facts->indirect_call_count;
            facts->needs_saved_return_address = true;
            break;
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
            ++facts->variadic_address_count;
            break;
        default:
            break;
        }
    }
    return true;
}
