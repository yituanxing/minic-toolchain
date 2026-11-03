#include "core/core_ir.h"
#include "compiler/core_function_set.h"
#include "frontend/ast.h"
#include "frontend/ast_verifier.h"
#include "frontend/cast_normalization.h"
#include "frontend/function_body.h"
#include "frontend/parser.h"
#include "target/riscv64/core_codegen.h"
#include "target/riscv64/core_admission.h"
#include "target/riscv64/function_abi_plan.h"
#include "target/target_info.h"
#include "target/source_policy/source_info.h"

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
    MinicTargetInvocation invocation;

    if (!minic_target_invocation_begin(
            &invocation, minic_target_info_for_architecture(MINIC_TARGET_ARCH_RISCV64)))
        return false;
    if (!minic_parse_c0_program(path, buffer->data, buffer->size, program, diagnostic) ||
        !minic_c0_program_verify_target(program, MINIC_C0_AST_PARSED, &invocation) ||
        !minic_c0_program_validate_function_body_ownership(program) ||
        !minic_c0_program_normalize_casts(program) ||
        !minic_c0_program_verify_target(program, MINIC_C0_AST_NORMALIZED, &invocation) ||
        !minic_c0_program_validate_function_body_ownership(program)) {
        return false;
    }
    return true;
}

static bool emit_frontend_functions(FILE *file, const MinicC0Program *program) {
    MinicCoreFunctionSet core_set;
    MinicTargetInvocation invocation;
    size_t function_index;
    size_t emitted_count;
    bool all_ok;

    minic_core_function_set_initialize(&core_set);
    if (!minic_target_invocation_begin(
            &invocation, minic_target_info_for_architecture(MINIC_TARGET_ARCH_RISCV64)) ||
        !minic_prepare_core_function_set(program, &invocation, &core_set)) {
        return false;
    }
    emitted_count = 0U;
    all_ok = true;
    for (function_index = 0U; function_index < program->function_count; ++function_index) {
        const MinicFunction *function;
        const MinicCoreFunction *sealed;
        MinicRiscvFunctionSymbol symbol;
        MinicRiscv64FunctionAbiPlan abi_plan = {0};
        MinicCoreModuleFunctionSymbol sealed_symbol = {0};
        bool success;

        function = minic_c0_program_function(program, function_index);
        if (function == NULL) {
            all_ok = false;
            break;
        }
        if (!function->is_defined || (function->is_internal && !function->is_referenced)) {
            continue;
        }
        sealed = &core_set.functions[function_index];
        /* Production's AST-to-Core transaction owns C type import, snapshots
         * and sealing. Target admission here must see the published Core. */
        /* Module descriptor owns its spelling in production; this fixture
         * borrows the source symbol for the duration of immediate emission. */
        sealed_symbol.name = (char *)minic_c0_function_symbol_name(function);
        sealed_symbol.section_name = function->section_name;
        sealed_symbol.explicit_alignment = function->explicit_alignment;
        sealed_symbol.is_internal = function->is_internal;
        sealed_symbol.is_weak = function->is_weak;
        sealed_symbol.visibility = (MinicCoreModuleVisibility)function->visibility;
        success = core_set.statuses[function_index] == MINIC_CORE_LOWER_OK &&
                  sealed->type_context != NULL &&
                  minic_core_function_verify_core_type_snapshot(sealed) &&
                  minic_core_function_verify(sealed) &&
                  minic_riscv64_core_function_admit(true, sealed, &abi_plan) &&
                  function->visibility <= MINIC_SYMBOL_VISIBILITY_PROTECTED &&
                  minic_riscv_function_symbol_from_module(&sealed_symbol, &symbol);
        minic_riscv64_function_abi_plan_destroy(&abi_plan);
        if (success) {
            success = minic_riscv64_emit_prepared_core_function_with_symbol(
                file, sealed, &symbol, false, NULL, NULL);
        }
        if (!success) {
            (void)fprintf(stderr,
                          "frontend Core emitter rejected function '%s' status=%d\n",
                          function->name != NULL ? function->name : "<unnamed>",
                          (int)core_set.statuses[function_index]);
        }
        if (!success) {
            all_ok = false;
            break;
        }
        emitted_count += 1U;
    }
    minic_core_function_set_destroy(&core_set);
    return all_ok && emitted_count != 0U;
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
