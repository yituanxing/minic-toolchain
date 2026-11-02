#ifndef MINIC_COMMON_CALLING_CONVENTION_H
#define MINIC_COMMON_CALLING_CONVENTION_H

#include <stdbool.h>

/* Execution calling-convention identity; physical argument assignment is
 * target-owned, and C prototype legality remains frontend-owned. */
typedef enum MinicFunctionCallingConvention {
    MINIC_CALLING_CONVENTION_DEFAULT = 0,
    MINIC_CALLING_CONVENTION_MS_X64
} MinicFunctionCallingConvention;

static inline bool minic_function_calling_convention_valid(
    MinicFunctionCallingConvention convention) {
    return convention == MINIC_CALLING_CONVENTION_DEFAULT ||
           convention == MINIC_CALLING_CONVENTION_MS_X64;
}

#endif
