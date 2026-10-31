#include "minic/compiler.h"
#include "compiler/core_function_set.h"
#include "compiler/inline_specialize.h"
#include "compiler/core_reachability.h"

#include "core/core_ir.h"
#include "core/core_lower.h"
#include "frontend/ast.h"
#include "frontend/ast_verifier.h"
#include "frontend/cast_normalization.h"
#include "frontend/const_eval.h"
#include "frontend/function_body.h"
#include "frontend/parser.h"
#include "target/backend.h"
#include "target/data_layout.h"
#include "target/target_info.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct MinicSourceBuffer {
    char *data;
    size_t size;
} MinicSourceBuffer;

/* Bootstrap diagnostics are deliberately opt-in.  They let a target-built
   compiler expose the exact phase/function where self-host replay stops
   making progress without changing normal compiler output or semantics. */
static bool minic_bootstrap_trace_enabled(void) {
    return getenv("MINIC_BOOTSTRAP_TRACE") != NULL;
}

static void minic_bootstrap_trace_stage(const char *input_path,
                                        const char *stage,
                                        const char *state,
                                        size_t function_count) {
    if (!minic_bootstrap_trace_enabled()) {
        return;
    }
    (void)fprintf(stderr,
                  "MINIC_BOOTSTRAP_TRACE stage=%s state=%s functions=%zu input=%s\n",
                  stage != NULL ? stage : "?",
                  state != NULL ? state : "?",
                  function_count,
                  input_path != NULL ? input_path : "?");
    (void)fflush(stderr);
}

static void minic_set_diagnostic(MinicDiagnostic *diagnostic,
                                 const char *path,
                                 size_t line,
                                 size_t column,
                                 const char *format,
                                 ...) {
    va_list arguments;

    if (diagnostic == NULL) {
        return;
    }
    diagnostic->path = path;
    diagnostic->line = line;
    diagnostic->column = column;
    va_start(arguments, format);
    (void)vsnprintf(diagnostic->message, sizeof(diagnostic->message), format, arguments);
    va_end(arguments);
}

static bool minic_validate_core_functions(const char *input_path,
                                          const MinicC0Program *program,
                                          const MinicCoreFunctionSet *set,
                                          MinicDiagnostic *diagnostic) {
    size_t function_index;
    const bool report_all = getenv("MINIC_CORE_FAILURE_CENSUS") != NULL;
    bool found_failure = false;

    if (program == NULL || set == NULL) {
        return false;
    }
    if (set->function_count != program->function_count ||
        (set->function_count != 0U &&
         (set->functions == NULL || set->statuses == NULL))) {
        minic_set_diagnostic(
            diagnostic, input_path, 1U, 1U, "Core IR functions do not match source program");
        return false;
    }
    for (function_index = 0U; function_index < program->function_count; ++function_index) {
        const MinicFunction *function;
        MinicCoreLowerStatus status;

        function = minic_c0_program_function(program, function_index);
        if (function == NULL) {
            minic_set_diagnostic(
                diagnostic, input_path, 1U, 1U, "Core lowering cannot access function");
            return false;
        }
        if (!function->is_defined) {
            continue;
        }
        if (function->is_internal && !function->is_referenced) {
            continue;
        }
        status = set->statuses[function_index];
        /* CoreLower already verified this successful function before
         * publishing LOWER_OK. The Common FunctionPlan verifies again for
         * standalone/output callers. Do not redundantly rescan it here. */
        if (status == MINIC_CORE_LOWER_OK) {
            continue;
        }
        /* The lowering pass has ALREADY evaluated every reachable C function.
         * Normal compilation remains fail-fast. On an explicit Linux corpus
         * diagnostic pass, enumerate EVERY failed Core function in this TU,
         * not merely the first diagnostic. This changes no IR or machine
         * semantics, nor does it mask any previously rejected function. */
        if (report_all) {
            (void)fprintf(stderr,
                "MINIC_CORE_FUNCTION_FAILURE kind=%s index=%zu function=%s\n",
                status == MINIC_CORE_LOWER_UNSUPPORTED ? "UNSUPPORTED" : "ERROR",
                function_index, function->name != NULL ? function->name : "?");
        }
        if (!found_failure) {
            if (status == MINIC_CORE_LOWER_UNSUPPORTED) {
                minic_set_diagnostic(diagnostic, input_path, 1U, 1U,
                    "Core IR does not yet support function '%s'",
                    function->name);
            } else {
                minic_set_diagnostic(diagnostic, input_path, 1U, 1U,
                    "Core IR lowering failed for function '%s'",
                    function->name);
            }
            found_failure = true;
        }
        if (!report_all)
            return false;
    }
    return !found_failure;
}

static bool
minic_read_file(const char *path, MinicSourceBuffer *buffer, MinicDiagnostic *diagnostic) {
    FILE *file;
    long end_position;
    size_t size;
    char *data;

    file = fopen(path, "rb");
    if (file == NULL) {
        minic_set_diagnostic(diagnostic, path, 1U, 1U, "cannot open input: %s", strerror(errno));
        return false;
    }
    if (fseek(file, 0L, SEEK_END) != 0 || (end_position = ftell(file)) < 0L ||
        fseek(file, 0L, SEEK_SET) != 0) {
        minic_set_diagnostic(diagnostic, path, 1U, 1U, "cannot seek input: %s", strerror(errno));
        (void)fclose(file);
        return false;
    }
    if ((unsigned long)end_position > (unsigned long)(SIZE_MAX - 1U)) {
        minic_set_diagnostic(diagnostic, path, 1U, 1U, "input is too large");
        (void)fclose(file);
        return false;
    }

    size = (size_t)end_position;
    data = (char *)malloc(size + 1U);
    if (data == NULL) {
        minic_set_diagnostic(diagnostic, path, 1U, 1U, "out of memory while reading input");
        (void)fclose(file);
        return false;
    }
    if (size != 0U && fread(data, 1U, size, file) != size) {
        minic_set_diagnostic(diagnostic, path, 1U, 1U, "cannot read input");
        free(data);
        (void)fclose(file);
        return false;
    }
    data[size] = '\0';
    if (fclose(file) != 0) {
        minic_set_diagnostic(diagnostic, path, 1U, 1U, "cannot close input: %s", strerror(errno));
        free(data);
        return false;
    }

    buffer->data = data;
    buffer->size = size;
    return true;
}

static void minic_set_ast_verify_diagnostic(const char *input_path,
                                            const char *form_name,
                                            const MinicC0AstVerifyFailure *failure,
                                            MinicDiagnostic *diagnostic) {
    const char *stage_name;
    const char *reason;

    stage_name = failure == NULL ? "unknown" : minic_c0_ast_verify_stage_name(failure->stage);
    reason = failure == NULL || failure->reason == NULL ? "contract violation" : failure->reason;
    if (failure != NULL && failure->index != MINIC_C0_AST_VERIFY_INDEX_NONE) {
        if (failure->subindex != MINIC_C0_AST_VERIFY_INDEX_NONE) {
            minic_set_diagnostic(diagnostic,
                                 input_path,
                                 1U,
                                 1U,
                                 "%s AST contract failed at %s[%zu:%zu]: %s",
                                 form_name,
                                 stage_name,
                                 failure->index,
                                 failure->subindex,
                                 reason);
        } else {
            minic_set_diagnostic(diagnostic,
                                 input_path,
                                 1U,
                                 1U,
                                 "%s AST contract failed at %s[%zu]: %s",
                                 form_name,
                                 stage_name,
                                 failure->index,
                                 reason);
        }
    } else {
        minic_set_diagnostic(diagnostic,
                             input_path,
                             1U,
                             1U,
                             "%s AST contract failed at %s: %s",
                             form_name,
                             stage_name,
                             reason);
    }
}

int minic_compile_preprocessed_file_for_target(const char *input_path,
                                               const char *output_path,
                                               const MinicTargetInfo *target_info,
                                               bool function_sections,
                                               MinicDiagnostic *diagnostic) {
    MinicSourceBuffer buffer;
    MinicC0Program program;
    MinicCoreFunctionSet core_set;
    MinicC0AstVerifyFailure verify_failure;
    const MinicBackend *backend;
    MinicInlineBooleanDomainStats boolean_stats = {0};
    bool success;

    if (input_path == NULL || output_path == NULL) {
        minic_set_diagnostic(diagnostic, input_path, 1U, 1U, "input and output paths are required");
        return 1;
    }
    if (diagnostic != NULL) {
        diagnostic->path = input_path;
        diagnostic->line = 1U;
        diagnostic->column = 1U;
        diagnostic->message[0] = '\0';
    }
    backend = minic_backend_for_target(target_info);
    if (target_info == NULL || backend == NULL ||
        target_info->data_layout == NULL || target_info->integer_model == NULL) {
        minic_set_diagnostic(diagnostic, input_path, 1U, 1U, "invalid target configuration");
        return 1;
    }
    /* The AST pass itself owns its short-lived BooleanDomain cache. */
    buffer.data = NULL;
    buffer.size = 0U;
    if (!minic_read_file(input_path, &buffer, diagnostic)) {
        return 1;
    }

    minic_c0_program_initialize(&program);
    minic_core_function_set_initialize(&core_set);
    minic_bootstrap_trace_stage(input_path, "parse", "begin", 0U);
    success = minic_parse_c0_program_for_target(
        input_path, buffer.data, buffer.size, target_info, &program, diagnostic);
    minic_bootstrap_trace_stage(input_path, "parse", success ? "end-ok" : "end-fail",
                                program.function_count);
    if (success && !minic_c0_program_verify_target_detailed(
                       &program, MINIC_C0_AST_PARSED, target_info, &verify_failure)) {
        minic_set_ast_verify_diagnostic(input_path, "parsed", &verify_failure, diagnostic);
        success = false;
    }
    if (success && !minic_c0_program_validate_function_body_ownership(&program)) {
        minic_set_diagnostic(
            diagnostic, input_path, 1U, 1U, "parsed FunctionBody ownership is invalid");
        success = false;
    }
    if (success) {
        minic_bootstrap_trace_stage(input_path, "normalize", "begin", program.function_count);
    }
    if (success && !minic_c0_program_normalize_casts(&program)) {
        minic_set_diagnostic(diagnostic, input_path, 1U, 1U, "cannot normalize cast expressions");
        success = false;
    }
    if (minic_bootstrap_trace_enabled()) {
        minic_bootstrap_trace_stage(input_path,
                                    "normalize",
                                    success ? "end-ok" : "end-fail",
                                    program.function_count);
    }
    if (success && !minic_c0_program_verify_target_detailed(
                       &program, MINIC_C0_AST_NORMALIZED, target_info, &verify_failure)) {
        minic_set_ast_verify_diagnostic(input_path, "normalized", &verify_failure, diagnostic);
        success = false;
    }
    if (success && !minic_c0_program_validate_function_body_ownership(&program)) {
        minic_set_diagnostic(
            diagnostic, input_path, 1U, 1U, "normalized FunctionBody ownership is invalid");
        success = false;
    }
    if (success && !minic_c0_program_recompute_inline_emission_references(&program)) {
        minic_set_diagnostic(diagnostic,
                             input_path,
                             1U,
                             1U,
                             "cannot compute inline emission reachability");
        success = false;
    }
    if (success && !minic_specialize_inline_integer_calls(&program, target_info)) {
        minic_set_diagnostic(diagnostic, input_path, 1U, 1U,
                             "cannot specialize inline integer call sites");
        success = false;
    }
    if (success && !minic_specialize_inline_symbolic_calls(&program, target_info)) {
        minic_set_diagnostic(diagnostic, input_path, 1U, 1U,
                             "cannot specialize inline symbolic address call sites");
        success = false;
    }
    if (success && !minic_specialize_inline_boolean_range_calls(&program, target_info, &boolean_stats)) {
        minic_set_diagnostic(diagnostic, input_path, 1U, 1U,
                             "cannot specialize inline boolean range call sites");
        success = false;
    }
    if (success && !minic_finalize_inline_integer_specialization_references(&program)) {
        minic_set_diagnostic(diagnostic, input_path, 1U, 1U,
                             "cannot finalize inline specialization reachability");
        success = false;
    }
    if (success) {
        minic_bootstrap_trace_stage(input_path, "core-set", "begin", program.function_count);
    }
    if (success && !minic_prepare_core_function_set(&program, target_info, &core_set)) {
        minic_set_diagnostic(
            diagnostic, input_path, 1U, 1U, "cannot retain Core IR lowering results");
        success = false;
    }
    if (minic_bootstrap_trace_enabled()) {
        minic_bootstrap_trace_stage(input_path,
                                    "core-set",
                                    success ? "end-ok" : "end-fail",
                                    program.function_count);
    }
    if (success && !minic_prune_inline_specializations_from_core(
                       &program, &core_set, backend->emit_reachable_core_blocks_only)) {
        minic_set_diagnostic(diagnostic,
                             input_path,
                             1U,
                             1U,
                             "cannot prune inline specialization Core reachability");
        success = false;
    }
    if (success && !minic_prune_dead_file_scope_static_objects_from_core(
                       &program, &core_set, backend->emit_reachable_core_blocks_only)) {
        minic_set_diagnostic(diagnostic, input_path, 1U, 1U,
                             "cannot compute static object/function Core reachability");
        success = false;
    }
    /* Unused file-static callback tables no longer seed their otherwise dead
     * static functions through initializer relocations. Reuse the existing
     * function reachability pass with the finalized object-emission policy. */
    if (success && !minic_prune_inline_specializations_from_core(
                       &program, &core_set, backend->emit_reachable_core_blocks_only)) {
        minic_set_diagnostic(diagnostic, input_path, 1U, 1U,
                             "cannot prune dead callback functions from Core");
        success = false;
    }
    if (success) {
        minic_bootstrap_trace_stage(input_path, "core-validate", "begin", program.function_count);
        success =
            minic_validate_core_functions(input_path, &program, &core_set, diagnostic);
        minic_bootstrap_trace_stage(input_path,
                                    "core-validate",
                                    success ? "end-ok" : "end-fail",
                                    program.function_count);
    }
    if (success) {
        MinicDataLayoutProgramCheck layout_status;
        minic_bootstrap_trace_stage(input_path, "layout", "begin", program.function_count);
        layout_status = minic_data_layout_validate_program(target_info->data_layout, &program);
        if (layout_status != MINIC_DATA_LAYOUT_PROGRAM_OK) {
            const char *reason = layout_status == MINIC_DATA_LAYOUT_PROGRAM_INVALID_RECORD
                ? "record size is invalid for the selected target"
                : layout_status == MINIC_DATA_LAYOUT_PROGRAM_INVALID_GLOBAL
                ? "global object size is invalid for the selected target"
                : "cannot validate target data layout";
            minic_set_diagnostic(diagnostic, input_path, 1U, 1U, reason);
            success = false;
        }
        minic_bootstrap_trace_stage(input_path, "layout",
                                    success ? "end-ok" : "end-fail",
                                    program.function_count);
    }
    if (success) {
        minic_bootstrap_trace_stage(input_path, "codegen", "begin", program.function_count);
        success = minic_backend_emit_assembly(
            backend, output_path, &program, target_info, core_set.functions,
            core_set.function_count, function_sections, diagnostic);
        minic_bootstrap_trace_stage(input_path,
                                    "codegen",
                                    success ? "end-ok" : "end-fail",
                                    program.function_count);
    }

    minic_core_function_set_destroy(&core_set);
    minic_c0_program_destroy(&program);
    if (getenv("MINIC_BOOLEAN_DOMAIN_PERF") != NULL) {
        minic_inline_boolean_domain_perf_dump(stderr, &boolean_stats);
    }
    free(buffer.data);
    return success ? 0 : 1;
}

/* Preserve existing RV64 APIs and GCC's default shared .text. */
int minic_compile_preprocessed_file_with_options(const char *input_path,
                                                 const char *output_path,
                                                 bool function_sections,
                                                 MinicDiagnostic *diagnostic) {
    return minic_compile_preprocessed_file_for_target(
        input_path, output_path, minic_default_target_info(), function_sections, diagnostic);
}

int minic_compile_preprocessed_file(const char *input_path,
                                    const char *output_path,
                                    MinicDiagnostic *diagnostic) {
    return minic_compile_preprocessed_file_with_options(
        input_path, output_path, false, diagnostic);
}
