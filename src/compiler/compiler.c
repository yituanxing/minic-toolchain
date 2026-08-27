#include "minic/compiler.h"

#include "core/core_ir.h"
#include "core/core_lower.h"
#include "frontend/ast.h"
#include "frontend/ast_verifier.h"
#include "frontend/cast_normalization.h"
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
        if (!minic_c0_function_body_view(program, function_index, &body)) {
            set.statuses[function_index] = MINIC_CORE_LOWER_ERROR;
            continue;
        }
        set.statuses[function_index] =
            minic_core_lower_function(&body, target, &set.functions[function_index]);
    }
    minic_core_function_set_destroy(output);
    *output = set;
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
    success = minic_parse_c0_program(input_path, buffer.data, buffer.size, &program, diagnostic);
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
    if (success && !minic_c0_program_normalize_casts(&program)) {
        minic_set_diagnostic(diagnostic, input_path, 1U, 1U, "cannot normalize cast expressions");
        success = false;
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
    if (success && !minic_prepare_core_function_set(&program, target_info, &core_set)) {
        minic_set_diagnostic(
            diagnostic, input_path, 1U, 1U, "cannot retain Core IR lowering results");
        success = false;
    }
    if (success) {
        success =
            minic_validate_core_functions(input_path, &program, &core_set, diagnostic);
    }
    if (success) {
        success = minic_riscv64_layout_program(input_path, &program, diagnostic);
    }
    if (success) {
        success =
            minic_riscv64_write_c0_program_with_core_functions(output_path,
                                                               &program,
                                                               core_set.functions,
                                                               core_set.function_count,
                                                               diagnostic);
    }

    minic_core_function_set_destroy(&core_set);
    minic_c0_program_destroy(&program);
    free(buffer.data);
    return success ? 0 : 1;
}
