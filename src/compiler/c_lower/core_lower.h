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

MinicCoreLowerStatus minic_core_lower_function(const MinicFunctionBodyView *body,
                                               const MinicTargetInvocation *target,
                                               MinicCoreSourceBuilder *output);
MinicCoreLowerStatus minic_core_lower_function_with_index(
    const MinicFunctionBodyView *body, const MinicTargetInvocation *target,
    MinicCoreSourceBuilder *output, MinicCoreReentryIndex *index);

#endif
