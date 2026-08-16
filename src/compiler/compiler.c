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

typedef enum MinicCoreShadowMode {
    MINIC_CORE_SHADOW_DISABLED = 0,
    MINIC_CORE_SHADOW_OPTIONAL,
    MINIC_CORE_SHADOW_STRICT
} MinicCoreShadowMode;

typedef struct MinicCoreCandidates {
    MinicCoreFunction *functions;
    MinicCoreLowerStatus *statuses;
    size_t function_count;
} MinicCoreCandidates;

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

static void minic_core_candidates_initialize(MinicCoreCandidates *candidates) {
    if (candidates == NULL) {
        return;
    }
    candidates->functions = NULL;
    candidates->statuses = NULL;
    candidates->function_count = 0U;
}

static void minic_core_candidates_destroy(MinicCoreCandidates *candidates) {
    size_t function_index;

    if (candidates == NULL) {
        return;
    }
    if (candidates->functions != NULL) {
        for (function_index = 0U; function_index < candidates->function_count; ++function_index) {
            minic_core_function_destroy(&candidates->functions[function_index]);
        }
    }
    free(candidates->functions);
    free(candidates->statuses);
    minic_core_candidates_initialize(candidates);
}

static bool minic_prepare_core_candidates(const MinicC0Program *program,
                                          MinicCoreCandidates *output) {
    MinicCoreCandidates candidates;
    size_t function_index;

    if (program == NULL || output == NULL ||
        program->function_count > SIZE_MAX / sizeof(*candidates.functions) ||
        program->function_count > SIZE_MAX / sizeof(*candidates.statuses)) {
        return false;
    }
    minic_core_candidates_initialize(&candidates);
    candidates.function_count = program->function_count;
    if (candidates.function_count != 0U) {
        candidates.functions =
            (MinicCoreFunction *)calloc(candidates.function_count, sizeof(*candidates.functions));
        candidates.statuses =
            (MinicCoreLowerStatus *)malloc(candidates.function_count * sizeof(*candidates.statuses));
        if (candidates.functions == NULL || candidates.statuses == NULL) {
            free(candidates.functions);
            free(candidates.statuses);
            return false;
        }
    }
    for (function_index = 0U; function_index < candidates.function_count; ++function_index) {
        minic_core_function_initialize(&candidates.functions[function_index]);
        candidates.statuses[function_index] = MINIC_CORE_LOWER_UNSUPPORTED;
    }
    for (function_index = 0U; function_index < candidates.function_count; ++function_index) {
        const MinicFunction *function;
        MinicFunctionBodyView body;

        function = minic_c0_program_function(program, function_index);
        if (function == NULL) {
            candidates.statuses[function_index] = MINIC_CORE_LOWER_ERROR;
            continue;
        }
        if (!function->is_defined) {
            continue;
        }
        if (!minic_c0_function_body_view(program, function_index, &body)) {
            candidates.statuses[function_index] = MINIC_CORE_LOWER_ERROR;
            continue;
        }
        candidates.statuses[function_index] =
            minic_core_lower_function(&body, &candidates.functions[function_index]);
    }
    minic_core_candidates_destroy(output);
    *output = candidates;
    return true;
}

static bool minic_core_shadow_mode(const char *input_path,
                                   MinicDiagnostic *diagnostic,
                                   MinicCoreShadowMode *mode) {
    const char *value;

    if (mode == NULL) {
        return false;
    }
    value = getenv("MINIC_CORE_IR");
    if (value == NULL || value[0] == '\0') {
        *mode = MINIC_CORE_SHADOW_DISABLED;
        return true;
    }
    if (strcmp(value, "shadow") == 0) {
        *mode = MINIC_CORE_SHADOW_OPTIONAL;
        return true;
    }
    if (strcmp(value, "strict") == 0) {
        *mode = MINIC_CORE_SHADOW_STRICT;
        return true;
    }
    minic_set_diagnostic(
        diagnostic, input_path, 1U, 1U, "MINIC_CORE_IR must be unset, 'shadow', or 'strict'");
    return false;
}

static bool minic_validate_core_shadow(const char *input_path,
                                       const MinicC0Program *program,
                                       const MinicCoreCandidates *candidates,
                                       MinicCoreShadowMode mode,
                                       MinicDiagnostic *diagnostic) {
    size_t function_index;

    if (program == NULL || candidates == NULL) {
        return false;
    }
    if (mode == MINIC_CORE_SHADOW_DISABLED) {
        return true;
    }
    if (candidates->function_count != program->function_count ||
        (candidates->function_count != 0U &&
         (candidates->functions == NULL || candidates->statuses == NULL))) {
        minic_set_diagnostic(
            diagnostic, input_path, 1U, 1U, "Core IR candidates do not match source program");
        return false;
    }
    for (function_index = 0U; function_index < program->function_count; ++function_index) {
        const MinicFunction *function;
        MinicCoreLowerStatus status;

        function = minic_c0_program_function(program, function_index);
        if (function == NULL) {
            minic_set_diagnostic(
                diagnostic, input_path, 1U, 1U, "Core IR shadow cannot access function");
            return false;
        }
        if (!function->is_defined) {
            continue;
        }
        status = candidates->statuses[function_index];
        if (status == MINIC_CORE_LOWER_OK &&
            !minic_core_function_verify(&candidates->functions[function_index])) {
            status = MINIC_CORE_LOWER_ERROR;
        }
        if (status == MINIC_CORE_LOWER_OK) {
            continue;
        }
        if (status == MINIC_CORE_LOWER_UNSUPPORTED && mode == MINIC_CORE_SHADOW_OPTIONAL) {
            continue;
        }
        if (status == MINIC_CORE_LOWER_UNSUPPORTED) {
            minic_set_diagnostic(diagnostic,
                                 input_path,
                                 1U,
                                 1U,
                                 "Core IR shadow does not yet support function '%s'",
                                 function->name);
            return false;
        }
        minic_set_diagnostic(diagnostic,
                             input_path,
                             1U,
                             1U,
                             "Core IR shadow lowering failed for function '%s'",
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

int minic_compile_preprocessed_file(const char *input_path,
                                    const char *output_path,
                                    MinicDiagnostic *diagnostic) {
    MinicSourceBuffer buffer;
    MinicC0Program program;
    MinicCoreCandidates core_candidates;
    const MinicTargetInfo *target_info;
    MinicCoreShadowMode core_shadow_mode;
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
    if (!minic_core_shadow_mode(input_path, diagnostic, &core_shadow_mode)) {
        return 1;
    }

    buffer.data = NULL;
    buffer.size = 0U;
    if (!minic_read_file(input_path, &buffer, diagnostic)) {
        return 1;
    }

    minic_c0_program_initialize(&program);
    minic_core_candidates_initialize(&core_candidates);
    target_info = minic_default_target_info();
    success = minic_parse_c0_program(input_path, buffer.data, buffer.size, &program, diagnostic);
    if (success && !minic_c0_program_verify_target(&program, MINIC_C0_AST_PARSED, target_info)) {
        minic_set_diagnostic(
            diagnostic, input_path, 1U, 1U, "parsed AST violates compiler contracts");
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
    if (success &&
        !minic_c0_program_verify_target(&program, MINIC_C0_AST_NORMALIZED, target_info)) {
        minic_set_diagnostic(
            diagnostic, input_path, 1U, 1U, "normalized AST violates backend contracts");
        success = false;
    }
    if (success && !minic_c0_program_validate_function_body_ownership(&program)) {
        minic_set_diagnostic(
            diagnostic, input_path, 1U, 1U, "normalized FunctionBody ownership is invalid");
        success = false;
    }
    if (success && core_shadow_mode != MINIC_CORE_SHADOW_DISABLED &&
        !minic_prepare_core_candidates(&program, &core_candidates)) {
        minic_set_diagnostic(
            diagnostic, input_path, 1U, 1U, "cannot retain Core IR lowering results");
        success = false;
    }
    if (success) {
        success = minic_validate_core_shadow(
            input_path, &program, &core_candidates, core_shadow_mode, diagnostic);
    }
    if (success) {
        success = minic_riscv64_layout_program(input_path, &program, diagnostic);
    }
    if (success) {
        success = minic_riscv64_write_c0_program(output_path, &program, diagnostic);
    }

    minic_core_candidates_destroy(&core_candidates);
    minic_c0_program_destroy(&program);
    free(buffer.data);
    return success ? 0 : 1;
}
