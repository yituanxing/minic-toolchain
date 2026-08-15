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

static bool minic_run_core_shadow(const char *input_path,
                                  const MinicC0Program *program,
                                  MinicCoreShadowMode mode,
                                  MinicDiagnostic *diagnostic) {
    size_t function_index;

    if (program == NULL || mode == MINIC_CORE_SHADOW_DISABLED) {
        return program != NULL;
    }
    for (function_index = 0U; function_index < program->function_count; ++function_index) {
        const MinicFunction *function;
        MinicFunctionBodyView body;
        MinicCoreFunction core;
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
        if (!minic_c0_function_body_view(program, function_index, &body)) {
            minic_set_diagnostic(diagnostic,
                                 input_path,
                                 1U,
                                 1U,
                                 "Core IR shadow cannot view function '%s'",
                                 function->name);
            return false;
        }
        minic_core_function_initialize(&core);
        status = minic_core_lower_function(&body, &core);
        if (status == MINIC_CORE_LOWER_OK && !minic_core_function_verify(&core)) {
            status = MINIC_CORE_LOWER_ERROR;
        }
        minic_core_function_destroy(&core);
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
    if (success) {
        success = minic_run_core_shadow(input_path, &program, core_shadow_mode, diagnostic);
    }
    if (success) {
        success = minic_riscv64_layout_program(input_path, &program, diagnostic);
    }
    if (success) {
        success = minic_riscv64_write_c0_program(output_path, &program, diagnostic);
    }

    minic_c0_program_destroy(&program);
    free(buffer.data);
    return success ? 0 : 1;
}
