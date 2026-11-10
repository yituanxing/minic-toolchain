#include "compiler/core_function_set.h"
#include "compiler/core_reachability.h"
#include "compiler/c_lower/core_type_import.h"
#include "compiler/c_lower/core_ir_source.h"
#include "compiler/module_ir_build.h"
#include "compiler/c_lower/reentry_index.h"
#include "frontend/function_body.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    set->attempted = NULL;
    set->function_count = 0U;
    set->type_context = NULL;
    set->type_importer = NULL;
    minic_core_module_initialize(&set->module);
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
    free(set->attempted);
    minic_core_module_destroy(&set->module);
    if (set->type_importer != NULL) {
        minic_core_type_importer_destroy(set->type_importer);
        free(set->type_importer);
    }
    if (set->type_context != NULL) {
        minic_core_type_context_destroy(set->type_context);
        free(set->type_context);
    }
    minic_core_function_set_initialize(set);
}

/* Both initially eligible source functions and later Core-closure discoveries
 * use this ONE transactional C->Core ingress. The ledger distinguishes a
 * dead function intentionally not attempted from a supported function whose
 * lowering legitimately failed. A failed draft never reaches executable Core. */
static void minic_core_set_lower_one(const MinicC0Program *program,
                                     const MinicTargetInvocation *target,
                                     MinicCoreFunctionSet *set,
                                     size_t function_index,
                                     MinicCoreReentryIndex *index,
                                     MinicCoreLowerWorkspace *workspace) {
    const MinicFunction *function;
    MinicFunctionBodyView body;
    MinicCoreSourceBuilder source_builder;

    if (function_index >= set->function_count || set->attempted[function_index])
        return;
    set->attempted[function_index] = true;
    function = minic_c0_program_function(program, function_index);
    if (function == NULL || !function->is_defined ||
        !minic_c0_function_body_view(program, function_index, &body)) {
        set->statuses[function_index] = MINIC_CORE_LOWER_ERROR;
        return;
    }
    if (minic_core_set_bootstrap_trace_enabled()) {
        (void)fprintf(stderr,
                      "MINIC_BOOTSTRAP_TRACE stage=core-lower state=begin "
                      "index=%zu function=%s\n",
                      function_index,
                      function->name != NULL ? function->name : "?");
        (void)fflush(stderr);
    }
    minic_core_source_builder_initialize(&source_builder);
    set->statuses[function_index] =
        minic_core_lower_function_with_workspace(
            &body, target, &source_builder, index, workspace);
    if (set->statuses[function_index] == MINIC_CORE_LOWER_OK &&
        (!minic_core_function_snapshot_types(
            &source_builder, set->type_context,
            minic_core_set_import_type, set->type_importer) ||
         !minic_core_function_verify(&source_builder.core) ||
         !minic_core_source_builder_take_core(
             &source_builder, &set->functions[function_index]))) {
        set->statuses[function_index] = MINIC_CORE_LOWER_ERROR;
    }
    minic_core_source_builder_destroy(&source_builder);
    if (set->statuses[function_index] == MINIC_CORE_LOWER_OK)
        (void)minic_core_function_fold_literal_branches(
            &set->functions[function_index]);
    if (minic_core_set_bootstrap_trace_enabled()) {
        (void)fprintf(stderr,
                      "MINIC_BOOTSTRAP_TRACE stage=core-lower state=end "
                      "index=%zu function=%s status=%d blocks=%zu instructions=%zu\n",
                      function_index,
                      function->name != NULL ? function->name : "?",
                      (int)set->statuses[function_index],
                      set->functions[function_index].block_count,
                      set->functions[function_index].instruction_count);
        (void)fflush(stderr);
    }
}

/* False means structural allocation failed and leaves output untouched. True
 * publishes one Core/status slot per FunctionId, even if individual function
 * lowerings recorded an error status; the Compiler validates those separately. */
bool minic_prepare_core_function_set(const MinicC0Program *program,
                                            const MinicTargetInvocation *target,
                                            MinicCoreFunctionSet *output) {
    MinicCoreFunctionSet set;
    MinicCoreTypeImporter *importer;
    MinicCoreReentryIndex *index;
    MinicCoreLowerWorkspace lowering_workspace;
    bool lowering_workspace_available;
    MinicCoreModuleFunctionNameIndex names = {0};
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
    set.type_importer = malloc(sizeof(*set.type_importer));
    if (set.type_importer == NULL) {
        minic_core_function_set_destroy(&set);
        return false;
    }
    importer = set.type_importer;
    if (!minic_core_type_importer_initialize(importer, program,
                                            target, set.type_context)) {
        minic_core_function_set_destroy(&set);
        return false;
    }
    set.function_count = program->function_count;
    if (set.function_count != 0U) {
        set.functions =
            (MinicCoreFunction *)calloc(set.function_count, sizeof(*set.functions));
        set.statuses = (MinicCoreLowerStatus *)malloc(set.function_count *
                                                       sizeof(*set.statuses));
        set.attempted = (bool *)calloc(set.function_count, sizeof(*set.attempted));
        if (set.functions == NULL || set.statuses == NULL || set.attempted == NULL) {
            minic_core_function_set_destroy(&set);
            return false;
        }
    }
    for (function_index = 0U; function_index < set.function_count; ++function_index) {
        minic_core_function_initialize(&set.functions[function_index]);
        set.statuses[function_index] = MINIC_CORE_LOWER_UNSUPPORTED;
    }
    /* Ingress owns all source-only non-Core roots before selecting which
     * canonical FunctionBodies to lower.  A typed ASM immediate or an
     * initializer &&label can require a static body even after ordinary C
     * lexical references were rewritten to specialized clones. */
    if (!minic_core_module_prepare_from_program(&set.module, program, importer)) {
        minic_core_function_set_destroy(&set);
        return false;
    }
    if (!minic_core_module_function_name_index_build(&set.module, &names)) {
        minic_core_function_set_destroy(&set);
        return false;
    }
    /* The AST statement/expression graph is immutable during Core Lowering.
     * Reuse one validated incoming-edge map for all ordinary and clone bodies. */
    index = minic_core_reentry_index_create(program);
    lowering_workspace_available = minic_core_lower_workspace_initialize(
        &lowering_workspace, program->statement_count);
    for (function_index = 0U; function_index < set.function_count; ++function_index) {
        const MinicFunction *function;

        function = minic_c0_program_function(program, function_index);
        if (function == NULL) {
            set.statuses[function_index] = MINIC_CORE_LOWER_ERROR;
            set.attempted[function_index] = true;
            continue;
        }
        if (!function->is_defined) {
            continue;
        }
        if (function->is_internal && !function->is_referenced &&
            !set.module.function_symbols[function_index].is_reachability_root &&
            !set.module.function_symbols[function_index].is_file_asm_root &&
            !set.module.function_symbols[function_index].is_owner_label_relocation_target) {
            const char *name = set.module.function_symbols[function_index].name;
            size_t first, last;
            /* An external declaration and a local definition may share an
             * assembler name even if their C identifiers differ. They have
             * distinct source-reference bits; lower a duplicate local body
             * speculatively so the later Core symbol closure can keep it if
             * a live CALL reaches that assembler name. Ordinary dead static
             * bodies still avoid all C-to-Core work. */
            if (name == NULL || !minic_core_module_function_name_index_find(
                    &names, name, strlen(name), &first, &last) ||
                last - first <= 1U)
                continue;
        }
        minic_core_set_lower_one(
            program, target, &set, function_index, index,
            lowering_workspace_available ? &lowering_workspace : NULL);
    }
    if (lowering_workspace_available)
        minic_core_lower_workspace_destroy(&lowering_workspace);
    minic_core_reentry_index_destroy(index);
    minic_core_module_function_name_index_destroy(&names);
    minic_core_function_set_destroy(output);
    *output = set;
    return true;
}

bool minic_complete_core_function_set_reachability(
    const MinicC0Program *program, const MinicTargetInvocation *target,
    MinicCoreFunctionSet *set, bool emit_reachable_core_blocks_only) {
    MinicCoreReentryIndex *index = NULL;
    MinicCoreLowerWorkspace lowering_workspace;
    bool lowering_workspace_available = false;
    bool lowering_workspace_initialized = false;
    size_t round, total_late_lowered = 0U;

    if (program == NULL || target == NULL || set == NULL ||
        set->type_context == NULL || set->type_importer == NULL ||
        set->module.finalized || set->function_count != program->function_count ||
        set->module.function_count != set->function_count ||
        (set->function_count != 0U &&
         (set->functions == NULL || set->statuses == NULL ||
          set->attempted == NULL || set->module.function_symbols == NULL)))
        return false;

    /* The closure itself consumes exclusively sealed Core and ModuleIR, but
     * an initial C emission hint may omit an internal function later reached
     * by a live Core CALL/ASM/global relocation (for example a GNU inline
     * wrapper after specialization). Its initial UNSUPPORTED status meant
     * "never attempted", not "invalid C". Re-enter the ONE source->Core
     * lowering transaction for exactly those newly live functions, then
     * recompute the Core-only closure: their own calls may discover another
     * late-live function. Each nonfinal round consumes at least one distinct
     * FunctionId, so the process is finite without sacrificing dead-body
     * pruning or allowing a failed Core draft to escape. */
    for (round = 0U; round <= set->function_count; ++round) {
        size_t function_index;
        bool added = false;

        if (!minic_core_function_set_close_reachability(
                set, emit_reachable_core_blocks_only))
            goto failed;
        for (function_index = 0U; function_index < set->function_count;
             ++function_index) {
            const MinicCoreModuleFunctionSymbol *symbol =
                &set->module.function_symbols[function_index];
            if (!symbol->is_defined || !symbol->is_referenced ||
                set->attempted[function_index])
                continue;
            if (index == NULL)
                index = minic_core_reentry_index_create(program);
            if (!lowering_workspace_initialized) {
                lowering_workspace_available =
                    minic_core_lower_workspace_initialize(
                        &lowering_workspace, program->statement_count);
                lowering_workspace_initialized = true;
            }
            minic_core_set_lower_one(
                program, target, set, function_index, index,
                lowering_workspace_available ? &lowering_workspace : NULL);
            added = true;
            ++total_late_lowered;
        }
        if (!added) {
            if (lowering_workspace_available)
                minic_core_lower_workspace_destroy(&lowering_workspace);
            minic_core_reentry_index_destroy(index);
            if (total_late_lowered != 0U)
                (void)fprintf(stderr,
                              "MINIC_CORE_LATE_LOWER_CLOSURE rounds=%zu functions=%zu\n",
                              round, total_late_lowered);
            return true;
        }
    }
failed:
    if (lowering_workspace_available)
        minic_core_lower_workspace_destroy(&lowering_workspace);
    minic_core_reentry_index_destroy(index);
    return false;
}

bool minic_finalize_core_function_set_module(const MinicC0Program *program,
                                            const MinicTargetInvocation *target,
                                            MinicCoreFunctionSet *set) {
    bool ok;
    if (program == NULL || target == NULL || set == NULL ||
        set->type_importer == NULL || set->type_context == NULL ||
        set->module.finalized || set->function_count != program->function_count)
        return false;
    ok = minic_core_module_materialize_live_payloads(
        &set->module, program, target, set->type_importer);
    if (ok)
        ok = minic_core_module_verify_label_bindings(
            &set->module, set->functions, set->function_count);
    if (ok)
        ok = minic_core_module_seal_source_asm_occurrences(
            &set->module, set->functions, set->function_count);
    /* The Core module is immutable after construction. Release the last
     * compiler-only AST importer before entering target emission. */
    minic_core_type_importer_destroy(set->type_importer);
    free(set->type_importer);
    set->type_importer = NULL;
    return ok;
}
