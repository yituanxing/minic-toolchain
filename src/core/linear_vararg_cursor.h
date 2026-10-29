#ifndef MINIC_CORE_LINEAR_VARARG_CURSOR_H
#define MINIC_CORE_LINEAR_VARARG_CURSOR_H

#include "core/core_lower_internal.h"

/* Source-to-Core helpers for ABI-selected linear va_list cursors. Both scalar
 * and aggregate va_arg must agree on alignment and slot consumption. */
MinicCoreLowerStatus minic_core_linear_vararg_prepare_cursor(
    MinicCoreLowerContext *context, MinicSourceSpan span,
    MinicCoreValueId cursor, MinicType cursor_type,
    size_t value_alignment, MinicCoreValueId *aligned_cursor);

MinicCoreLowerStatus minic_core_linear_vararg_advance(
    MinicCoreLowerContext *context, MinicSourceSpan span,
    MinicCoreValueId cursor, MinicCoreValueId list_address,
    MinicType cursor_type, size_t value_size, bool is_volatile);

#endif
