#include "compiler/inline_specialize.h"
#include "compiler/inline_specialize_internal.h"
#include "frontend/const_eval.h"
#include "frontend/function_body.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Compiler AST pass: remaining non-Core reference roots for inline clones.
 * Function bodies remain from the validated monolithic optimizer. */
static bool minic_inline_integer_source_has_residual_reference(
    const MinicC0Program *program, MinicFunctionId source_id) {
    bool *owner_live;
    size_t expression_index;
    size_t function_index;
    size_t object_index;

    if (program == NULL || source_id >= program->function_count) {
        return true;
    }
    if (program->entry_function == source_id || program->functions[source_id].force_emit) {
        return true;
    }
    for (function_index = 0U; function_index < program->function_count; ++function_index) {
        if (program->functions[function_index].alias_target == source_id) {
            return true;
        }
    }
    owner_live = minic_c0_program_build_global_owner_live_map(program);
    for (object_index = 0U; object_index < program->global_object_count; ++object_index) {
        const MinicGlobalObject *object = &program->global_objects[object_index];
        size_t relocation_index;
        if (!minic_c0_program_global_object_emittable_with_map(
                program, object, owner_live))
            continue;
        for (relocation_index = 0U; relocation_index < object->relocation_count;
             ++relocation_index) {
            const MinicGlobalRelocation *relocation = &object->relocations[relocation_index];
            if (relocation->target_kind == MINIC_GLOBAL_RELOCATION_FUNCTION &&
                (MinicFunctionId)relocation->target_id == source_id) {
                free(owner_live);
                return true;
            }
        }
    }
    free(owner_live);
    for (expression_index = 0U; expression_index < program->expression_count;
         ++expression_index) {
        const MinicExpression *expression = &program->expressions[expression_index];
        if ((expression->kind == MINIC_EXPRESSION_FUNCTION &&
             expression->value.function_id == source_id) ||
            (expression->kind == MINIC_EXPRESSION_CALL &&
             expression->value.call.function_id == source_id)) {
            return true;
        }
    }
    return false;
}

bool minic_finalize_inline_integer_specialization_references(MinicC0Program *program) {
    unsigned char *source_residual_cache = NULL;
    bool cache_allocation_failed = false;
    size_t function_index;

    if (program == NULL) {
        return false;
    }
    /* Generic reachability ran before cloning, while FunctionBody ownership was still
     * one-to-one. Preserve that result for clones first, then retire a source only when
     * specialization rewrote every syntactic/function-address reference to it. */
    for (function_index = 0U; function_index < program->function_count; ++function_index) {
        MinicFunction *function = &program->functions[function_index];
        if (function->is_integer_specialization) {
            MinicFunctionId source_id = function->specialization_source;
            if (source_id >= program->function_count ||
                program->functions[source_id].is_integer_specialization) {
                return false;
            }
            function->is_referenced = program->functions[source_id].is_referenced;
        }
    }
    /* Calls and function-address references are no longer rewritten after
     * specialization. All clones of one source therefore ask the same question.
     * Cache only within this finalization pass, after validating source IDs;
     * on allocation failure, retain the original source-wide scan. */
    for (function_index = 0U; function_index < program->function_count; ++function_index) {
        MinicFunction *function = &program->functions[function_index];
        if (function->is_integer_specialization) {
            MinicFunctionId source_id = function->specialization_source;
            MinicFunction *source = &program->functions[source_id];
            if (source->is_internal && source->is_inline && source->is_referenced) {
                bool has_residual_reference;
                if (source_residual_cache == NULL && !cache_allocation_failed) {
                    source_residual_cache = (unsigned char *)calloc(
                        program->function_count == 0U ? 1U : program->function_count,
                        sizeof(*source_residual_cache));
                    cache_allocation_failed = source_residual_cache == NULL;
                }
                if (source_residual_cache != NULL &&
                    source_residual_cache[source_id] != 0U) {
                    has_residual_reference = source_residual_cache[source_id] == 1U;
                } else {
                    has_residual_reference =
                        minic_inline_integer_source_has_residual_reference(program, source_id);
                    if (source_residual_cache != NULL) {
                        source_residual_cache[source_id] =
                            has_residual_reference ? 1U : 2U;
                    }
                }
                if (!has_residual_reference) {
                    source->is_referenced = false;
                }
            }
        }
    }
    free(source_residual_cache);
    return true;
}


