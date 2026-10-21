#ifndef MINIC_COMPILER_INLINE_SPECIALIZE_H
#define MINIC_COMPILER_INLINE_SPECIALIZE_H
#include "frontend/ast.h"
#include "target/target_info.h"
#include <stdbool.h>
#include <stdio.h>

/* AST-stage, function-cloning passes. Do not run after Core lowering.
 * Per-compilation boolean-domain cache is explicitly reset at TU bounds.
 * The backend never sees private specialization construction helpers. */
bool minic_specialize_inline_integer_calls(MinicC0Program *program,
                                                  const MinicTargetInfo *target);
bool minic_specialize_inline_symbolic_calls(
    MinicC0Program *program,
    const MinicTargetInfo *target);
void minic_inline_local_boolean_domain_cache_clear(void);
void minic_inline_boolean_domain_perf_dump(FILE *out);
bool minic_specialize_inline_boolean_range_calls(
    MinicC0Program *program,
    const MinicTargetInfo *target);
bool minic_finalize_inline_integer_specialization_references(MinicC0Program *program);
#endif
