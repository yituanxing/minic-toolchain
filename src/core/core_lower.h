#ifndef MINIC_CORE_CORE_LOWER_H
#define MINIC_CORE_CORE_LOWER_H

#include "core/core_ir.h"
#include "frontend/function_body.h"
#include "target/target_info.h"

typedef enum MinicCoreLowerStatus {
    MINIC_CORE_LOWER_OK = 0,
    MINIC_CORE_LOWER_UNSUPPORTED,
    MINIC_CORE_LOWER_ERROR
} MinicCoreLowerStatus;

/* Optional scoped read-only reentry index for one compilation's Core Set.
 * begin() falls back to existing per-query scans on validation/OOM failure. */
void minic_core_lower_reentry_index_begin(const MinicC0Program *program);
void minic_core_lower_reentry_index_end(void);

MinicCoreLowerStatus minic_core_lower_function(const MinicFunctionBodyView *body,
                                               const MinicTargetInfo *target,
                                               MinicCoreFunction *output);

#endif
