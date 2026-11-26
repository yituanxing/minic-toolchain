#ifndef MINIC_COMPILER_INLINE_SPECIALIZE_INTERNAL_H
#define MINIC_COMPILER_INLINE_SPECIALIZE_INTERNAL_H
#include "compiler/inline_specialize_common.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* One constructor for all source specialization facts. */
bool minic_static_integer_specialization_eligible(const MinicFunction *source);
bool minic_inline_spec_add_variant(
    MinicC0Program *program, MinicFunctionId source_id,
    const MinicFunction *caller, const MinicInlineSpecializationKey *key,
    unsigned int fact_mask, size_t ordinal, MinicFunctionId *specialized_id);
bool minic_inline_spec_resolve_call(
    const MinicInlineCallPlan *plan, MinicExpressionId expression_id,
    size_t source_limit, MinicFunctionId *current_id, MinicFunctionId *source_id,
    MinicFunction *current, MinicFunction *source);
#endif
