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
/* Compiler-internal AST->Core per-TU function lifecycle. The outgoing
 * statuses/verified Core IR are consumed by independent backend targets. */
void minic_core_function_set_initialize(MinicCoreFunctionSet *set);
void minic_core_function_set_destroy(MinicCoreFunctionSet *set);
bool minic_prepare_core_function_set(const MinicC0Program *program,
                                     const MinicTargetInfo *target,
                                     MinicCoreFunctionSet *output);

#endif
