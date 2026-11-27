#ifndef MINIC_TARGET_COMMON_CORE_FUNCTION_FACTS_H
#define MINIC_TARGET_COMMON_CORE_FUNCTION_FACTS_H

#include "core/core_ir.h"
#include "target/common/core_call_view.h"
#include <stdbool.h>
#include <stddef.h>

/* Read-only Core semantic facts: NOT an ISA capability or psABI plan.
 * ISA legalizations, register banks and frame layouts stay target-owned. */
typedef struct MinicCoreFunctionFacts {
    size_t direct_call_count;
    size_t indirect_call_count;
    /* Visitor-owned, immutable semantic views for each Core CALL. */
    const MinicCoreCallIndex *call_index;
    /* Successful canonical verifier proof for this immutable definition. */
    const MinicCoreFunction *verified_function;
    /* Borrowed from the prepared function owner, for this verified Core definition. */
    const bool *reachable_blocks;
    const size_t *value_use_counts;
    size_t variadic_address_count;
    /* Visitor-owned checked va_start result slots; NULL when count is zero. */
    const MinicCoreValueId *variadic_address_results;
    bool needs_saved_return_address;
    bool uses_structured_inline_asm;
    bool uses_dynamic_stack_alloc;
} MinicCoreFunctionFacts;

/* Prepared Core admission is valid only for the exact same immutable function. */
static inline bool minic_core_function_facts_verified(
    const MinicCoreFunctionFacts *facts, const MinicCoreFunction *function) {
    return facts != NULL && function != NULL &&
           facts->verified_function == function;
}

/* One checked scan collects Core facts and all va_start result IDs.
 * Caller owns *variadic_results, initially NULL. */
bool minic_core_function_facts_analyze(
    const MinicCoreFunction *, MinicCoreFunctionFacts *,
    MinicCoreValueId **variadic_results);

#endif
