#include "compiler/core_function_set.h"
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

void minic_core_function_set_initialize(MinicCoreFunctionSet *set) {
    if (set == NULL) {
        return;
    }
    set->functions = NULL;
    set->statuses = NULL;
    set->function_count = 0U;
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
    minic_core_function_set_initialize(set);
}

/* False means structural allocation failed and leaves output untouched. True
 * publishes one Core/status slot per FunctionId, even if individual function
 * lowerings recorded an error status; the Compiler validates those separately. */
bool minic_prepare_core_function_set(const MinicC0Program *program,
                                            const MinicTargetInfo *target,
                                            MinicCoreFunctionSet *output) {
    MinicCoreFunctionSet set;
    MinicCoreReentryIndex *index;
    size_t function_index;

    if (program == NULL || target == NULL || output == NULL ||
        program->function_count > SIZE_MAX / sizeof(*set.functions) ||
        program->function_count > SIZE_MAX / sizeof(*set.statuses)) {
        return false;
    }
    minic_core_function_set_initialize(&set);
    set.function_count = program->function_count;
    if (set.function_count != 0U) {
        set.functions =
            (MinicCoreFunction *)calloc(set.function_count, sizeof(*set.functions));
        set.statuses = (MinicCoreLowerStatus *)malloc(set.function_count *
                                                       sizeof(*set.statuses));
        if (set.functions == NULL || set.statuses == NULL) {
            free(set.functions);
            free(set.statuses);
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
    minic_core_function_set_destroy(output);
    *output = set;
    return true;
}
