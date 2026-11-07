#ifndef MINIC_CORE_CONST_GLOBAL_RECORD_H
#define MINIC_CORE_CONST_GLOBAL_RECORD_H

#include "compiler/c_lower/core_lower_internal.h"

/* Read a scalar member of a fully initialized, immutable file/static-local
 * record array when the array index and member path are proven fixed. This
 * remains a source-level Core fact, independent of target machine lowering. */
bool minic_core_const_global_record_scalar(
    const MinicCoreLowerContext *context, MinicExpressionId expression_id,
    MinicConstValue *value);

/* Pure static-inline getter of an in-bounds, TU-local immutable array
 * element, with every reachable guard proven non-effectful. This is only a
 * CFG purity fact; it never invents an integer value or rewrites Core IR. */
bool minic_core_const_global_record_pure_getter(
    const MinicCoreLowerContext *context, MinicExpressionId call_id);

#endif
