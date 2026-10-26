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
    size_t max_call_arity; /* max source arguments, not ABI stack slots */
    /* Visitor-owned, immutable semantic views for each Core CALL. NULL only
     * for standalone compatibility analyzers outside production visitor. */
    const MinicCoreCallIndex *call_index;
    /* Successful canonical verifier proof for this immutable definition. */
    const MinicCoreFunction *verified_function;
    size_t variadic_address_count;
    bool needs_saved_return_address;
    bool uses_structured_inline_asm;
    bool uses_dynamic_stack_alloc;
} MinicCoreFunctionFacts;

/* Prepared Core admission is valid only for the exact same immutable
 * function. Standalone compatibility emitters carry no proof and must
 * execute the canonical verifier normally. */
static inline bool minic_core_function_facts_verified(
    const MinicCoreFunctionFacts *facts, const MinicCoreFunction *function) {
    return facts != NULL && function != NULL &&
           facts->verified_function == function;
}

/* Single checked walk; returns false if Core instruction backing is absent. */
bool minic_core_function_facts_analyze(
    const MinicCoreFunction *, MinicCoreFunctionFacts *);

#endif
