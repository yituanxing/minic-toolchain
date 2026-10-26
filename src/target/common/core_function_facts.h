#ifndef MINIC_TARGET_COMMON_CORE_FUNCTION_FACTS_H
#define MINIC_TARGET_COMMON_CORE_FUNCTION_FACTS_H

#include "core/core_ir.h"
#include <stdbool.h>
#include <stddef.h>

/* Read-only Core semantic facts: NOT an ISA capability or psABI plan.
 * ISA legalizations, register banks and frame layouts stay target-owned. */
typedef struct MinicCoreFunctionFacts {
    size_t direct_call_count;
    size_t indirect_call_count;
    size_t max_call_arity; /* max source arguments, not ABI stack slots */
    size_t variadic_address_count;
    bool needs_saved_return_address;
    bool uses_structured_inline_asm;
    bool uses_dynamic_stack_alloc;
} MinicCoreFunctionFacts;

/* Single checked walk; returns false if Core instruction backing is absent. */
bool minic_core_function_facts_analyze(
    const MinicCoreFunction *, MinicCoreFunctionFacts *);

#endif
