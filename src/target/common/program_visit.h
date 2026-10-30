#ifndef MINIC_TARGET_COMMON_PROGRAM_VISIT_H
#define MINIC_TARGET_COMMON_PROGRAM_VISIT_H

#include "core/core_ir.h"
#include "target/common/core_function_plan.h"
#include "frontend/ast.h"
#include "target/target_info.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

/* A single ISA/ABI-independent translation-unit traversal for all five MiniC
 * targets. Only declaration filtering and visitation order live here.
 * No frame offsets, registers, calling convention or assembler syntax.
 * The visitor controls source-asm provenance, global symbols and diagnostics.
 * Different targets may retain distinct ordering of external declarations. */
typedef bool (*MinicProgramVisitGlobal)(
    FILE *, const MinicC0Program *, const MinicTargetInfo *,
    const MinicGlobalObject *, size_t, void *);
typedef bool (*MinicProgramVisitFunction)(
    FILE *, const MinicC0Program *, const MinicTargetInfo *,
    const MinicFunction *, const MinicCoreFunction *, size_t,
    bool function_sections, void *);

/* Every emitted Core definition has one ABI-neutral semantic-facts plan.
 * Targets consume its call/variadic/dynamic stack facts without rescanning.
 * Physical locations and ISA admission remain target responsibilities. */
typedef bool (*MinicProgramVisitPreparedFunction)(
    FILE *, const MinicC0Program *, const MinicTargetInfo *,
    const MinicFunction *, const MinicCoreFunction *,
    const MinicCoreFunctionFacts *, const MinicCoreObjectStoragePlan *,
    size_t, bool function_sections, void *);

typedef enum MinicProgramFunctionSelection {
    MINIC_PROGRAM_FUNCTION_ALL,
    MINIC_PROGRAM_FUNCTION_DECLARATIONS,
    MINIC_PROGRAM_FUNCTION_LIVE_DEFINITIONS
} MinicProgramFunctionSelection;

/* The declaration-only and prepared-definition passes must share the same
 * selection semantics, including unreachable static definitions. */
static inline bool minic_program_function_selected(
    const MinicFunction *decl, MinicProgramFunctionSelection selection) {
    return decl != NULL &&
        !(selection == MINIC_PROGRAM_FUNCTION_DECLARATIONS && decl->is_defined) &&
        !(selection == MINIC_PROGRAM_FUNCTION_LIVE_DEFINITIONS && !decl->is_defined) &&
        !(decl->is_defined && decl->is_internal && !decl->is_referenced);
}

static inline bool minic_program_visit_globals(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    MinicProgramVisitGlobal visit, void *context) {
    size_t i;
    bool *live_owners;
    bool success = true;
    if (out == NULL || program == NULL || visit == NULL) return false;
    /* Snapshot emitted owners once: GNU Linux TUs may contain thousands of
     * static-local objects and inline clones. The hot visitor is O(F + G),
     * not O(F * G); allocation failure uses the exact uncached predicate. */
    live_owners = minic_c0_program_build_global_owner_live_map(program);
    for (i = 0U; i < program->global_object_count; ++i) {
        const MinicGlobalObject *object = &program->global_objects[i];
        if (!minic_c0_program_global_object_emittable_with_map(
                program, object, live_owners))
            continue;
        if (!visit(out, program, target, object, i, context)) {
            success = false;
            break;
        }
    }
    free(live_owners);
    return success;
}

static inline bool minic_program_visit_functions(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *functions, size_t function_count,
    bool function_sections, MinicProgramFunctionSelection selection,
    bool continue_after_failure, MinicProgramVisitFunction visit,
    void *context) {
    size_t i;
    bool success = true;
    if (out == NULL || program == NULL || visit == NULL ||
        function_count != program->function_count ||
        (function_count != 0U && functions == NULL))
        return false;
    for (i = 0U; i < function_count; ++i) {
        const MinicFunction *decl = &program->functions[i];
        if (!minic_program_function_selected(decl, selection))
            continue;
        if (!visit(out, program, target, decl, &functions[i], i,
                   function_sections, context)) {
            success = false;
            if (!continue_after_failure) break;
        }
    }
    return success;
}

/* The single production per-function preparation stage. No ABI assumptions,
 * no target-specific opcode mapping, and no writable Core state. A NULL
 * facts pointer denotes a non-definition (RV64 declaration pass). */
static inline bool minic_program_visit_prepared_functions(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *functions, size_t function_count,
    bool function_sections, MinicProgramFunctionSelection selection,
    bool continue_after_failure, MinicProgramVisitPreparedFunction visit,
    void *context) {
    size_t i;
    bool success = true;
    if (out == NULL || program == NULL || visit == NULL || target == NULL ||
        function_count != program->function_count ||
        (function_count != 0U && functions == NULL))
        return false;
    for (i = 0U; i < function_count; ++i) {
        const MinicFunction *decl = &program->functions[i];
        MinicCoreFunctionPlan plan = {0};
        bool ready = true;
        if (!minic_program_function_selected(decl, selection))
            continue;
        /* One common, ABI-neutral function-plan owner. Target frames borrow
         * the immutable layout/semantic snapshot for this visitor call only.
         * No physical ABI state escapes into the Core definition. */
        if (decl->is_defined)
            ready = minic_core_function_plan_build(
                program, target, &functions[i], &plan);
        if (ready)
            ready = visit(out, program, target, decl, &functions[i],
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
