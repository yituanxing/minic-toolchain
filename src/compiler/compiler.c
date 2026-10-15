#include "minic/compiler.h"

#include "core/core_ir.h"
#include "core/core_lower.h"
#include "frontend/ast.h"
#include "frontend/ast_verifier.h"
#include "frontend/cast_normalization.h"
#include "frontend/const_eval.h"
#include "frontend/function_body.h"
#include "frontend/parser.h"
#include "target/riscv64/codegen.h"
#include "target/riscv64/layout.h"
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

typedef struct MinicCoreFunctionSet {
    MinicCoreFunction *functions;
    MinicCoreLowerStatus *statuses;
    size_t function_count;
} MinicCoreFunctionSet;

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

static void minic_core_function_set_initialize(MinicCoreFunctionSet *set) {
    if (set == NULL) {
        return;
    }
    set->functions = NULL;
    set->statuses = NULL;
    set->function_count = 0U;
}

static void minic_core_function_set_destroy(MinicCoreFunctionSet *set) {
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

static bool minic_prepare_core_function_set(const MinicC0Program *program,
                                            const MinicTargetInfo *target,
                                            MinicCoreFunctionSet *output) {
    MinicCoreFunctionSet set;
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
        if (minic_bootstrap_trace_enabled()) {
            (void)fprintf(stderr,
                          "MINIC_BOOTSTRAP_TRACE stage=core-lower state=begin "
                          "index=%zu function=%s\n",
                          function_index,
                          function->name != NULL ? function->name : "?");
            (void)fflush(stderr);
        }
        set.statuses[function_index] =
            minic_core_lower_function(&body, target, &set.functions[function_index]);
        if (minic_bootstrap_trace_enabled()) {
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
    minic_core_function_set_destroy(output);
    *output = set;
    return true;
}



#define MINIC_INLINE_INTEGER_SPECIALIZATION_LIMIT 256U

static bool minic_inline_integer_specialization_matches(
    const MinicFunction *candidate,
    MinicFunctionId source_id,
    const bool *known,
    const uint64_t *bits,
    size_t parameter_count) {
    size_t i;
    if (candidate == NULL || !candidate->is_integer_specialization ||
        candidate->specialization_source != source_id ||
        candidate->parameter_count != parameter_count) {
        return false;
    }
    for (i = 0U; i < parameter_count; ++i) {
        if (candidate->specialization_integer_known[i] != known[i] ||
            (known[i] && candidate->specialization_integer_bits[i] != bits[i])) {
            return false;
        }
    }
    return true;
}

static bool minic_add_inline_integer_specialization(
    MinicC0Program *program,
    MinicFunctionId source_id,
    const bool *known,
    const uint64_t *bits,
    size_t ordinal,
    MinicFunctionId *specialized_id) {
    MinicFunction source;
    MinicFunction *specialized;
    char name[96];
    int name_length;
    size_t i;

    if (program == NULL || source_id >= program->function_count ||
        specialized_id == NULL) {
        return false;
    }
    source = program->functions[source_id];
    if (!source.is_defined || !source.is_internal || !source.is_inline ||
        source.is_variadic || source.alias_target != MINIC_FUNCTION_INVALID) {
        return false;
    }
    name_length = snprintf(name, sizeof(name), "__minic_inline_spec_%zu_%zu",
                           (size_t)source_id, ordinal);
    if (name_length <= 0 || (size_t)name_length >= sizeof(name) ||
        !minic_c0_program_add_function(program, name, (size_t)name_length,
                                       source.local_begin, source.local_count,
                                       source.body_block, specialized_id) ||
        !minic_c0_program_set_function_signature(program, *specialized_id,
                                                 source.return_type,
                                                 source.parameter_types,
                                                 source.parameter_count) ||
        !minic_c0_program_set_function_internal(program, *specialized_id, true) ||
        !minic_c0_program_set_function_inline(program, *specialized_id, true) ||
        (source.is_noreturn &&
         !minic_c0_program_set_function_noreturn(program, *specialized_id, true)) ||
        (source.section_name != NULL &&
         !minic_c0_program_set_function_section(program, *specialized_id,
                                                source.section_name,
                                                source.section_name_length))) {
        return false;
    }
    specialized = &program->functions[*specialized_id];
    specialized->visibility = source.visibility;
    specialized->specialization_source = source_id;
    specialized->is_integer_specialization = true;
    for (i = 0U; i < source.parameter_count; ++i) {
        specialized->specialization_integer_known[i] = known[i];
        specialized->specialization_integer_bits[i] = bits[i];
    }
    return true;
}

static bool minic_specialize_inline_integer_calls(MinicC0Program *program,
                                                  const MinicTargetInfo *target) {
    size_t expression_index;
    size_t original_function_count;
    size_t specialization_count = 0U;

    if (program == NULL || target == NULL) {
        return false;
    }
    original_function_count = program->function_count;
    for (expression_index = 0U; expression_index < program->expression_count;
         ++expression_index) {
        MinicExpression *expression = &program->expressions[expression_index];
        MinicFunctionId source_id;
        MinicFunctionId specialized_id = MINIC_FUNCTION_INVALID;
        MinicFunction source;
        bool known[MINIC_MAX_FUNCTION_PARAMETERS];
        uint64_t bits[MINIC_MAX_FUNCTION_PARAMETERS];
        size_t i;
        size_t candidate_index;
        bool has_integer_constant = false;

        if (expression->kind != MINIC_EXPRESSION_CALL ||
            expression->value.call.function_id == MINIC_FUNCTION_INVALID) {
            continue;
        }
        source_id = expression->value.call.function_id;
        if (source_id >= original_function_count) {
            continue;
        }
        source = program->functions[source_id];
        if (!source.is_defined || !source.is_internal || !source.is_inline ||
            !source.is_referenced || source.is_variadic ||
            source.alias_target != MINIC_FUNCTION_INVALID || source.parameter_count == 0U ||
            source.parameter_count != expression->value.call.argument_count) {
            continue;
        }
        (void)memset(known, 0, sizeof(known));
        (void)memset(bits, 0, sizeof(bits));
        for (i = 0U; i < source.parameter_count; ++i) {
            MinicConstValue argument_value;
            MinicConstValue converted_value;
            if (!minic_type_is_integer(source.parameter_types[i]) ||
                !minic_const_eval_integer(program, target,
                                          expression->value.call.arguments[i],
                                          &argument_value) ||
                !minic_const_value_convert_integer(program, target, &argument_value,
                                                   source.parameter_types[i],
                                                   &converted_value)) {
                continue;
            }
            known[i] = true;
            bits[i] = converted_value.bits;
            has_integer_constant = true;
        }
        if (!has_integer_constant) {
            continue;
        }
        for (candidate_index = original_function_count;
             candidate_index < program->function_count; ++candidate_index) {
            if (minic_inline_integer_specialization_matches(
                    &program->functions[candidate_index], source_id, known, bits,
                    source.parameter_count)) {
                specialized_id = candidate_index;
                break;
            }
        }
        if (specialized_id == MINIC_FUNCTION_INVALID) {
            if (specialization_count >= MINIC_INLINE_INTEGER_SPECIALIZATION_LIMIT ||
                !minic_add_inline_integer_specialization(program, source_id, known, bits,
                                                         specialization_count,
                                                         &specialized_id)) {
                return false;
            }
            specialization_count += 1U;
        }
        program->expressions[expression_index].value.call.function_id = specialized_id;
    }
    if (specialization_count != 0U) {
        (void)fprintf(stderr,
                      "MINIC_INLINE_INTEGER_SPECIALIZATION_V0 clones=%zu functions=%zu\n",
                      specialization_count, program->function_count);
    }
    return true;
}

static bool minic_inline_integer_source_has_residual_reference(
    const MinicC0Program *program, MinicFunctionId source_id) {
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
    for (object_index = 0U; object_index < program->global_object_count; ++object_index) {
        const MinicGlobalObject *object = &program->global_objects[object_index];
        size_t relocation_index;
        for (relocation_index = 0U; relocation_index < object->relocation_count;
             ++relocation_index) {
            const MinicGlobalRelocation *relocation = &object->relocations[relocation_index];
            if (relocation->target_kind == MINIC_GLOBAL_RELOCATION_FUNCTION &&
                (MinicFunctionId)relocation->target_id == source_id) {
                return true;
            }
        }
    }
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

static bool minic_finalize_inline_integer_specialization_references(MinicC0Program *program) {
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
    for (function_index = 0U; function_index < program->function_count; ++function_index) {
        MinicFunction *function = &program->functions[function_index];
        if (function->is_integer_specialization) {
            MinicFunctionId source_id = function->specialization_source;
            MinicFunction *source = &program->functions[source_id];
            if (source->is_internal && source->is_inline && source->is_referenced &&
                !minic_inline_integer_source_has_residual_reference(program, source_id)) {
                source->is_referenced = false;
            }
        }
    }
    return true;
}

static bool minic_validate_core_functions(const char *input_path,
                                          const MinicC0Program *program,
                                          const MinicCoreFunctionSet *set,
                                          MinicDiagnostic *diagnostic) {
    size_t function_index;

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
        if (status == MINIC_CORE_LOWER_OK &&
            !minic_core_function_verify(&set->functions[function_index])) {
            status = MINIC_CORE_LOWER_ERROR;
        }
        if (status == MINIC_CORE_LOWER_OK) {
            continue;
        }
        if (status == MINIC_CORE_LOWER_UNSUPPORTED) {
            minic_set_diagnostic(diagnostic,
                                 input_path,
                                 1U,
                                 1U,
                                 "Core IR does not yet support function '%s'",
                                 function->name);
            return false;
        }
        minic_set_diagnostic(diagnostic,
                             input_path,
                             1U,
                             1U,
                             "Core IR lowering failed for function '%s'",
                             function->name);
        return false;
    }
    return true;
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

int minic_compile_preprocessed_file(const char *input_path,
                                    const char *output_path,
                                    MinicDiagnostic *diagnostic) {
    MinicSourceBuffer buffer;
    MinicC0Program program;
    MinicCoreFunctionSet core_set;
    const MinicTargetInfo *target_info;
    MinicC0AstVerifyFailure verify_failure;
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
    buffer.data = NULL;
    buffer.size = 0U;
    if (!minic_read_file(input_path, &buffer, diagnostic)) {
        return 1;
    }

    minic_c0_program_initialize(&program);
    minic_core_function_set_initialize(&core_set);
    target_info = minic_default_target_info();
    minic_bootstrap_trace_stage(input_path, "parse", "begin", 0U);
    success = minic_parse_c0_program(input_path, buffer.data, buffer.size, &program, diagnostic);
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
        minic_bootstrap_trace_stage(input_path, "layout", "begin", program.function_count);
        success = minic_riscv64_layout_program(input_path, &program, diagnostic);
        minic_bootstrap_trace_stage(input_path,
                                    "layout",
                                    success ? "end-ok" : "end-fail",
                                    program.function_count);
    }
    if (success) {
        minic_bootstrap_trace_stage(input_path, "codegen", "begin", program.function_count);
        success =
            minic_riscv64_write_c0_program_with_core_functions(output_path,
                                                               &program,
                                                               core_set.functions,
                                                               core_set.function_count,
                                                               diagnostic);
        minic_bootstrap_trace_stage(input_path,
                                    "codegen",
                                    success ? "end-ok" : "end-fail",
                                    program.function_count);
    }

    minic_core_function_set_destroy(&core_set);
    minic_c0_program_destroy(&program);
    free(buffer.data);
    return success ? 0 : 1;
}
