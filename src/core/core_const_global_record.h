#ifndef MINIC_CORE_CONST_GLOBAL_RECORD_H
#define MINIC_CORE_CONST_GLOBAL_RECORD_H

#include "core/core_lower_internal.h"

/* Read a scalar member of a fully initialized, immutable file/static-local
 * record array when the array index and member path are proven fixed. This
 * remains a source-level Core fact, independent of target machine lowering. */
bool minic_core_const_global_record_scalar(
    const MinicCoreLowerContext *context, MinicExpressionId expression_id,
    MinicConstValue *value);

#endif
