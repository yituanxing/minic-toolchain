#ifndef MINIC_CORE_CORE_LOWER_H
#define MINIC_CORE_CORE_LOWER_H

#include "compiler/c_lower/core_ir_source.h"
#include "compiler/c_lower/reentry_index.h"
#include "frontend/function_body.h"
#include "target/source_policy/source_info.h"

typedef enum MinicCoreLowerStatus {
    MINIC_CORE_LOWER_OK = 0,
    MINIC_CORE_LOWER_UNSUPPORTED,
    MINIC_CORE_LOWER_ERROR
} MinicCoreLowerStatus;

/* The source->Core pass may lower hundreds of functions from one immutable
 * translation unit. Reuse the statement-id lookup across functions, clearing
 * only entries the previous lowering wrote. This is compiler-only state and
 * never becomes part of Core or Machine IR. */
typedef struct MinicCoreLowerWorkspace {
    MinicCoreBlockId *statement_blocks;
    MinicStatementId *touched_statements;
    size_t statement_count;
    size_t touched_count;
} MinicCoreLowerWorkspace;

bool minic_core_lower_workspace_initialize(MinicCoreLowerWorkspace *workspace,
                                          size_t statement_count);
void minic_core_lower_workspace_destroy(MinicCoreLowerWorkspace *workspace);
MinicCoreLowerStatus minic_core_lower_function_with_workspace(
    const MinicFunctionBodyView *body, const MinicTargetInvocation *target,
    MinicCoreSourceBuilder *output, MinicCoreReentryIndex *index,
    MinicCoreLowerWorkspace *workspace);

MinicCoreLowerStatus minic_core_lower_function(const MinicFunctionBodyView *body,
                                               const MinicTargetInvocation *target,
                                               MinicCoreSourceBuilder *output);
MinicCoreLowerStatus minic_core_lower_function_with_index(
    const MinicFunctionBodyView *body, const MinicTargetInvocation *target,
    MinicCoreSourceBuilder *output, MinicCoreReentryIndex *index);

#endif
