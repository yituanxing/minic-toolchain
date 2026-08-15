#ifndef MINIC_CORE_CORE_LOWER_H
#define MINIC_CORE_CORE_LOWER_H

#include "core/core_ir.h"
#include "frontend/function_body.h"

typedef enum MinicCoreLowerStatus {
    MINIC_CORE_LOWER_OK = 0,
    MINIC_CORE_LOWER_UNSUPPORTED,
    MINIC_CORE_LOWER_ERROR
} MinicCoreLowerStatus;

MinicCoreLowerStatus minic_core_lower_function(const MinicFunctionBodyView *body,
                                               MinicCoreFunction *output);

#endif
