#ifndef MINIC_FRONTEND_FUNCTION_BODY_H
#define MINIC_FRONTEND_FUNCTION_BODY_H

#include "frontend/ast.h"

typedef struct MinicFunctionBodyView {
    const MinicC0Program *program;
    MinicFunctionId function_id;
} MinicFunctionBodyView;

/*
 * FunctionBody is a logical ownership view over the current Program representation. It does not
 * promise contiguous expression, statement or block ranges. The existing LocalId range remains
 * an explicit MinicFunction contract until local storage ownership is migrated separately.
 */
bool minic_c0_function_body_view(const MinicC0Program *program,
                                 MinicFunctionId function_id,
                                 MinicFunctionBodyView *view);
const MinicFunction *minic_c0_function_body_function(const MinicFunctionBodyView *view);
MinicBlockId minic_c0_function_body_root_block(const MinicFunctionBodyView *view);

/* Whole-Program, source-LocalId parameter immutability oracle. The caller
 * supplies a local_count-sized bitmap and may cache it for an AST/Core pass.
 * Exactly the named source parameters with NO syntactic write, increment,
 * address escape, or inline-asm output receive true. All other locals are
 * false; uncertainty is conservative. One pass over the Program arenas, not
 * one full-body search per argument or specialized clone. */
bool minic_c0_program_stable_parameter_locals(
    const MinicC0Program *program, bool *stable, size_t stable_count);

/* Perf-only helper used by the Linux specialization audit.  Owners are
 * source-function ids; MINIC_FUNCTION_INVALID means unknown/orphan and
 * program->function_count is reserved internally for ambiguous shared nodes. */
bool minic_perf_program_build_expression_source_owners(
    const MinicC0Program *program,
    size_t function_limit,
    MinicFunctionId *owners,
    size_t owner_count);

/* Verify structural function-body ownership and function-local semantic references. */
bool minic_c0_program_validate_function_body_ownership(const MinicC0Program *program);

/* Recompute static-inline emission from semantic reachability. Parser-time
 * references are lexical and may originate in dead header inline bodies;
 * only references reachable from functions that are emitted independently,
 * global function-pointer initializers, aliases, or the entry function should
 * cause an internal inline definition to be emitted. */
bool minic_c0_program_recompute_inline_emission_references(MinicC0Program *program);

#endif
