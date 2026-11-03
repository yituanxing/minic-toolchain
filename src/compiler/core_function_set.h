#ifndef MINIC_COMPILER_CORE_FUNCTION_SET_H
#define MINIC_COMPILER_CORE_FUNCTION_SET_H
#include "core/core_ir.h"
#include "core/module_ir.h"
#include "compiler/c_lower/core_lower.h"
#include <stddef.h>

/* Translation-unit-aligned Core functions and their lowering verdicts.
 * This compiler-internal lifetime container never carries ISA registers. */
typedef struct MinicCoreFunctionSet {
    MinicCoreFunction *functions;
    MinicCoreLowerStatus *statuses;
    size_t function_count;
    /* One type arena per translation unit. Heap storage keeps the address
     * stable while the function set is transactionally moved to its caller. */
    MinicCoreTypeContext *type_context;
    /* Compiler-only source ingress survives reachability pruning so that
     * module globals share nominal CoreType IDs with function snapshots. */
    struct MinicCoreTypeImporter *type_importer;
    MinicCoreModule module;
} MinicCoreFunctionSet;
/* Compiler-internal AST->Core per-TU function lifecycle. The outgoing
 * statuses/verified Core IR are consumed by independent backend targets. */
void minic_core_function_set_initialize(MinicCoreFunctionSet *set);
void minic_core_function_set_destroy(MinicCoreFunctionSet *set);
bool minic_prepare_core_function_set(const MinicC0Program *program,
                                     const MinicTargetInvocation *target,
                                     MinicCoreFunctionSet *output);
/* Final C->Core module snapshot, after static pruning and DataLayout checks.
 * On failure the existing CoreFunctionSet remains owned by its caller. */
bool minic_finalize_core_function_set_module(const MinicC0Program *program,
                                            const MinicTargetInvocation *target,
                                            MinicCoreFunctionSet *set);

#endif
