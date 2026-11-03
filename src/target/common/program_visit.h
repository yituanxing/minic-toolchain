#ifndef MINIC_TARGET_COMMON_PROGRAM_VISIT_H
#define MINIC_TARGET_COMMON_PROGRAM_VISIT_H

#include "core/core_ir.h"
#include "core/module_ir.h"
#include "target/common/core_function_plan.h"
#include "target/target_info.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/* One ABI-neutral, ModuleIR-owned function/global walk. Each selected
 * function borrows a single prepared Core facts/storage plan; ISA-specific
 * register, frame, instruction and assembler policy remain callback-owned. */
typedef bool (*MinicProgramVisitModuleGlobal)(
    FILE *, const MinicCoreModule *, const MinicTargetInfo *,
    const MinicCoreModuleGlobal *, size_t, void *);
typedef bool (*MinicProgramVisitPreparedFunction)(
    FILE *, const MinicCoreModule *, const MinicTargetInfo *,
    const MinicCoreModuleFunctionSymbol *, const MinicCoreFunction *,
    const MinicCoreFunctionFacts *, const MinicCoreObjectStoragePlan *,
    size_t, bool function_sections, void *);

typedef enum MinicProgramFunctionSelection {
    MINIC_PROGRAM_FUNCTION_ALL,
    MINIC_PROGRAM_FUNCTION_DECLARATIONS,
    MINIC_PROGRAM_FUNCTION_LIVE_DEFINITIONS
} MinicProgramFunctionSelection;

bool minic_program_module_function_selected(
    const MinicCoreModuleFunctionSymbol *decl,
    MinicProgramFunctionSelection selection);
bool minic_program_visit_module_globals(
    FILE *out, const MinicCoreModule *module, const MinicTargetInfo *target,
    MinicProgramVisitModuleGlobal visit, void *context);
bool minic_program_visit_prepared_functions_module(
    FILE *out, const MinicCoreModule *module, const MinicTargetInfo *target,
    const MinicCoreFunction *functions, size_t function_count,
    bool function_sections, MinicProgramFunctionSelection selection,
    bool continue_after_failure, MinicProgramVisitPreparedFunction visit,
    void *context);

#endif
