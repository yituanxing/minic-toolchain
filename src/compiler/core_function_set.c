#include "compiler/core_function_set.h"
#include "core/core_type_import.h"
#include "core/reentry_index.h"
#include "frontend/function_body.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* The compiler owns the AST->Core function-set transaction. It brackets the
 * optional source incoming-edge index once per compilation and preserves
 * the immutable AST/clone loop, result statuses, trace and cleanup semantics.
 * This module is not an architectural Machine IR nor an optimization pass. */
static bool minic_core_set_bootstrap_trace_enabled(void) {
    return getenv("MINIC_BOOTSTRAP_TRACE") != NULL;
}

/* The only source-to-execution-type conversion in the compiler driver.
 * Neither the Core verifier nor any Target query needs a C Program to
 * interpret a Core type identity after this snapshot is attached. */
static bool minic_core_set_import_type(void *data, MinicType source,
                                      MinicCoreTypeId *result) {
    return minic_core_type_import((MinicCoreTypeImporter *)data, source, result);
}

void minic_core_function_set_initialize(MinicCoreFunctionSet *set) {
    if (set == NULL) {
        return;
    }
    set->functions = NULL;
    set->statuses = NULL;
    set->function_count = 0U;
    set->type_context = NULL;
}

void minic_core_function_set_destroy(MinicCoreFunctionSet *set) {
    size_t function_index;

    if (set == NULL) {
        return;
    }
    if (set->functions != NULL) {
        for (function_index = 0U; function_index < set->function_count; ++function_index) {
            minic_core_function_destroy(&set->functions[function_index]);
        }
    }
    free(set->functions);
    free(set->statuses);
    if (set->type_context != NULL) {
        minic_core_type_context_destroy(set->type_context);
        free(set->type_context);
    }
    minic_core_function_set_initialize(set);
}

/* False means structural allocation failed and leaves output untouched. True
 * publishes one Core/status slot per FunctionId, even if individual function
 * lowerings recorded an error status; the Compiler validates those separately. */
bool minic_prepare_core_function_set(const MinicC0Program *program,
                                            const MinicTargetInfo *target,
                                            MinicCoreFunctionSet *output) {
    MinicCoreFunctionSet set;
    MinicCoreTypeImporter importer;
    MinicCoreReentryIndex *index;
    size_t function_index;

    if (program == NULL || target == NULL || output == NULL ||
        program->function_count > SIZE_MAX / sizeof(*set.functions) ||
        program->function_count > SIZE_MAX / sizeof(*set.statuses)) {
        return false;
    }
    minic_core_function_set_initialize(&set);
    set.type_context = (MinicCoreTypeContext *)malloc(sizeof(*set.type_context));
    if (set.type_context == NULL) return false;
    minic_core_type_context_initialize(set.type_context);
    if (!minic_core_type_importer_initialize(&importer, program,
                                            target->data_layout, set.type_context)) {
        minic_core_type_importer_destroy(&importer);
        minic_core_function_set_destroy(&set);
        return false;
    }
    set.function_count = program->function_count;
    if (set.function_count != 0U) {
        set.functions =
            (MinicCoreFunction *)calloc(set.function_count, sizeof(*set.functions));
        set.statuses = (MinicCoreLowerStatus *)malloc(set.function_count *
                                                       sizeof(*set.statuses));
        if (set.functions == NULL || set.statuses == NULL) {
            minic_core_type_importer_destroy(&importer);
            minic_core_function_set_destroy(&set);
            return false;
        }
    }
    for (function_index = 0U; function_index < set.function_count; ++function_index) {
        minic_core_function_initialize(&set.functions[function_index]);
        set.statuses[function_index] = MINIC_CORE_LOWER_UNSUPPORTED;
    }
    /* The AST statement/expression graph is immutable during Core Lowering.
     * Reuse one validated incoming-edge map for all ordinary and clone bodies. */
    index = minic_core_reentry_index_create(program);
    for (function_index = 0U; function_index < set.function_count; ++function_index) {
        const MinicFunction *function;
        MinicFunctionBodyView body;

        function = minic_c0_program_function(program, function_index);
        if (function == NULL) {
            set.statuses[function_index] = MINIC_CORE_LOWER_ERROR;
            continue;
        }
        if (!function->is_defined) {
            continue;
        }
        if (function->is_internal && !function->is_referenced) {
            continue;
        }
        if (!minic_c0_function_body_view(program, function_index, &body)) {
            set.statuses[function_index] = MINIC_CORE_LOWER_ERROR;
            continue;
        }
        if (minic_core_set_bootstrap_trace_enabled()) {
            (void)fprintf(stderr,
                          "MINIC_BOOTSTRAP_TRACE stage=core-lower state=begin "
                          "index=%zu function=%s\n",
                          function_index,
                          function->name != NULL ? function->name : "?");
            (void)fflush(stderr);
        }
        set.statuses[function_index] =
            minic_core_lower_function_with_index(&body, target,
                                                &set.functions[function_index], index);
        if (set.statuses[function_index] == MINIC_CORE_LOWER_OK &&
            (!minic_core_function_snapshot_types(
                &set.functions[function_index], set.type_context,
                minic_core_set_import_type, &importer) ||
             !minic_core_function_verify(&set.functions[function_index]))) {
            set.statuses[function_index] = MINIC_CORE_LOWER_ERROR;
        }
        if (set.statuses[function_index] == MINIC_CORE_LOWER_OK)
            (void)minic_core_function_fold_literal_branches(
                &set.functions[function_index]);
        if (minic_core_set_bootstrap_trace_enabled()) {
            (void)fprintf(stderr,
                          "MINIC_BOOTSTRAP_TRACE stage=core-lower state=end "
                          "index=%zu function=%s status=%d blocks=%zu instructions=%zu\n",
                          function_index,
                          function->name != NULL ? function->name : "?",
                          (int)set.statuses[function_index],
                          set.functions[function_index].block_count,
                          set.functions[function_index].instruction_count);
            (void)fflush(stderr);
        }
    }
    minic_core_reentry_index_destroy(index);
    minic_core_type_importer_destroy(&importer);
    minic_core_function_set_destroy(output);
    *output = set;
    return true;
}
