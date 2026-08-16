#include "core/core_ir.h"
#include "core/core_lower.h"
#include "frontend/ast.h"
#include "frontend/ast_verifier.h"
#include "frontend/cast_normalization.h"
#include "frontend/function_body.h"
#include "frontend/parser.h"
#include "target/riscv64/core_codegen.h"
#include "target/target_info.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct TestSourceBuffer {
    char *data;
    size_t size;
} TestSourceBuffer;

static bool read_source(const char *path, TestSourceBuffer *buffer) {
    FILE *file;
    long end_position;
    size_t size;
    char *data;

    if (path == NULL || buffer == NULL) {
        return false;
    }
    file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }
    if (fseek(file, 0L, SEEK_END) != 0 || (end_position = ftell(file)) < 0L ||
        fseek(file, 0L, SEEK_SET) != 0) {
        (void)fclose(file);
        return false;
    }
    if ((unsigned long)end_position > (unsigned long)(SIZE_MAX - 1U)) {
        (void)fclose(file);
        return false;
    }
    size = (size_t)end_position;
    data = (char *)malloc(size + 1U);
    if (data == NULL) {
        (void)fclose(file);
        return false;
    }
    if (size != 0U && fread(data, 1U, size, file) != size) {
        free(data);
        (void)fclose(file);
        return false;
    }
    data[size] = '\0';
    if (fclose(file) != 0) {
        free(data);
        return false;
    }
    buffer->data = data;
    buffer->size = size;
    return true;
}

static bool prepare_program(const char *path,
                            const TestSourceBuffer *buffer,
                            MinicC0Program *program,
                            MinicDiagnostic *diagnostic) {
    const MinicTargetInfo *target_info;

    target_info = minic_default_target_info();
    if (!minic_parse_c0_program(path, buffer->data, buffer->size, program, diagnostic) ||
        !minic_c0_program_verify_target(program, MINIC_C0_AST_PARSED, target_info) ||
        !minic_c0_program_validate_function_body_ownership(program) ||
        !minic_c0_program_normalize_casts(program) ||
        !minic_c0_program_verify_target(program, MINIC_C0_AST_NORMALIZED, target_info) ||
        !minic_c0_program_validate_function_body_ownership(program)) {
        return false;
    }
    return true;
}

static bool emit_frontend_functions(FILE *file, const MinicC0Program *program) {
    size_t function_index;
    size_t emitted_count;

    emitted_count = 0U;
    for (function_index = 0U; function_index < program->function_count; ++function_index) {
        const MinicFunction *function;
        MinicFunctionBodyView body;
        MinicCoreFunction core;
        MinicCoreLowerStatus status;
        MinicRiscv64FunctionSymbol symbol;
        bool success;

        function = minic_c0_program_function(program, function_index);
        if (function == NULL) {
            return false;
        }
        if (!function->is_defined) {
            continue;
        }
        if (!minic_c0_function_body_view(program, function_index, &body)) {
            return false;
        }
        minic_core_function_initialize(&core);
        status = minic_core_lower_function(&body, &core);
        success = status == MINIC_CORE_LOWER_OK && minic_core_function_verify(&core) &&
                  minic_riscv64_core_function_can_emit_basic_v0(&core) &&
                  minic_riscv64_function_symbol_from_function(function, &symbol);
        if (success) {
            success = minic_riscv64_emit_core_function_basic_v0_with_symbol(file, &core, &symbol);
        }
        if (!success) {
            (void)fprintf(stderr,
                          "frontend Core emitter rejected function '%s' status=%d\n",
                          function->name != NULL ? function->name : "<unnamed>",
                          (int)status);
        }
        minic_core_function_destroy(&core);
        if (!success) {
            return false;
        }
        emitted_count += 1U;
    }
    return emitted_count != 0U;
}

int main(int argc, char **argv) {
    TestSourceBuffer buffer;
    MinicC0Program program;
    MinicDiagnostic diagnostic;
    FILE *file;
    bool success;

    if (argc != 3) {
        return 2;
    }
    buffer.data = NULL;
    buffer.size = 0U;
    (void)memset(&diagnostic, 0, sizeof(diagnostic));
    if (!read_source(argv[1], &buffer)) {
        return 3;
    }

    minic_c0_program_initialize(&program);
    success = prepare_program(argv[1], &buffer, &program, &diagnostic);
    if (!success) {
        (void)fprintf(stderr,
                      "%s:%zu:%zu: %s\n",
                      diagnostic.path != NULL ? diagnostic.path : argv[1],
                      diagnostic.line,
                      diagnostic.column,
                      diagnostic.message[0] != '\0' ? diagnostic.message
                                                    : "frontend preparation failed");
    }

    file = NULL;
    if (success) {
        file = fopen(argv[2], "w");
        success = file != NULL;
    }
    if (success) {
        success = emit_frontend_functions(file, &program);
    }
    if (file != NULL && fclose(file) != 0) {
        success = false;
    }

    minic_c0_program_destroy(&program);
    free(buffer.data);
    return success ? 0 : 1;
}
