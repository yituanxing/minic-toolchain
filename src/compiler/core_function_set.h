#ifndef MINIC_COMPILER_CORE_FUNCTION_SET_H
#define MINIC_COMPILER_CORE_FUNCTION_SET_H
#include "core/core_ir.h"
#include "core/core_lower.h"
#include <stddef.h>

/* Translation-unit-aligned Core functions and their lowering verdicts.
 * This compiler-internal lifetime container never carries ISA registers. */
typedef struct MinicCoreFunctionSet {
    MinicCoreFunction *functions;
    MinicCoreLowerStatus *statuses;
    size_t function_count;
} MinicCoreFunctionSet;
#endif
