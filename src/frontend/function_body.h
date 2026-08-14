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
bool minic_c0_function_body_owns_local(const MinicFunctionBodyView *view, MinicLocalId local_id);

/* Verify structural function-body ownership and function-local semantic references. */
bool minic_c0_program_validate_function_body_ownership(const MinicC0Program *program);

#endif
