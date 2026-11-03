#ifndef MINIC_TARGET_COMMON_PROGRAM_VISIT_H
#define MINIC_TARGET_COMMON_PROGRAM_VISIT_H

#include "core/core_ir.h"
#include "core/module_ir.h"
#include "target/common/core_function_plan.h"
#include "target/target_info.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/* A sealed translation unit is the only machine-side declaration owner.
 * The visitor chooses which definitions to emit and constructs one shared,
 * ABI-neutral Core plan. Targets alone own ABI and instructions. */
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

static inline bool minic_program_module_function_selected(
    const MinicCoreModuleFunctionSymbol *decl,
    MinicProgramFunctionSelection selection) {
    return decl != NULL &&
        !(selection == MINIC_PROGRAM_FUNCTION_DECLARATIONS && decl->is_defined) &&
        !(selection == MINIC_PROGRAM_FUNCTION_LIVE_DEFINITIONS && !decl->is_defined) &&
        !(decl->is_defined && decl->is_internal && !decl->is_referenced);
}

static inline bool minic_program_visit_module_globals(
    FILE *out, const MinicCoreModule *module, const MinicTargetInfo *target,
    MinicProgramVisitModuleGlobal visit, void *context) {
    size_t index;
    if (out == NULL || target == NULL || module == NULL || !module->finalized ||
        visit == NULL || (module->global_count != 0U && module->globals == NULL))
        return false;
    for (index = 0U; index < module->global_count; ++index) {
        const MinicCoreModuleGlobal *object = &module->globals[index];
        if (object->emittable &&
            !visit(out, module, target, object, index, context))
            return false;
    }
    return true;
}

static inline bool minic_program_visit_prepared_functions_module(
    FILE *out, const MinicCoreModule *module, const MinicTargetInfo *target,
    const MinicCoreFunction *functions, size_t function_count,
    bool function_sections, MinicProgramFunctionSelection selection,
    bool continue_after_failure, MinicProgramVisitPreparedFunction visit,
    void *context) {
    size_t i;
    bool success = true;
    if (out == NULL || visit == NULL || target == NULL || module == NULL ||
        !module->finalized || module->function_count != function_count ||
        (function_count != 0U &&
         (module->function_symbols == NULL || functions == NULL)))
        return false;
    for (i = 0U; i < function_count; ++i) {
        const MinicCoreModuleFunctionSymbol *decl = &module->function_symbols[i];
        MinicCoreFunctionPlan plan = {0};
        bool ready = true;
        if (!minic_program_module_function_selected(decl, selection))
            continue;
        if (decl->is_defined)
            ready = minic_core_function_plan_build(&functions[i], &plan);
        if (ready)
            ready = visit(out, module, target, decl, &functions[i],
                          decl->is_defined ? &plan.facts : NULL,
                          decl->is_defined ? &plan.objects : NULL, i,
                          function_sections, context);
        minic_core_function_plan_destroy(&plan);
        if (!ready) {
            success = false;
            if (!continue_after_failure) break;
        }
    }
    return success;
}

#endif
