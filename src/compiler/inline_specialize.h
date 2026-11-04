#ifndef MINIC_COMPILER_INLINE_SPECIALIZE_H
#define MINIC_COMPILER_INLINE_SPECIALIZE_H
#include "frontend/ast.h"
#include "compiler/inline_specialize_common.h"
#include "target/source_policy/source_info.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/* AST-stage, function-cloning passes. Do not run after Core lowering.
 * Boolean-domain cache has explicit per-invocation ownership.
 * The backend never sees private specialization construction helpers. */
bool minic_specialize_inline_integer_calls(MinicC0Program *program,
    const MinicTargetInvocation *target, MinicInlineCallPlan *call_plan);
bool minic_specialize_inline_symbolic_calls(
    MinicC0Program *program,
    const MinicTargetInvocation *target, MinicInlineCallPlan *call_plan);
typedef struct MinicInlineBooleanDomainStats {
    size_t queries;
    size_t builds;
    size_t fallbacks;
} MinicInlineBooleanDomainStats;

void minic_inline_boolean_domain_perf_dump(
    FILE *out, const MinicInlineBooleanDomainStats *stats);
bool minic_specialize_inline_boolean_range_calls(
    MinicC0Program *program, const MinicTargetInvocation *target,
    MinicInlineCallPlan *call_plan, MinicInlineBooleanDomainStats *stats_out);
#endif
