#include "target/riscv64/codegen.h"
#include "target/riscv64/global_emit.h"
#include "target/common/global_emit.h"
#include "target/common/elf_alias_emit.h"
#include "target/common/program_visit.h"
#include "target/common/program_emit.h"
#include "target/riscv64/abi.h"
#include "target/riscv64/codegen_internal.h"
#include "target/riscv64/core_codegen.h"
#include "target/riscv/function_symbol.h"
#include "target/data_layout.h"
#include "target/riscv64/isa.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The five targets now use one program visitor. Keep RISC-V LP64 global
 * relocations, GNU file-asm provenance, symbol syntax and ABI self-contained. */
typedef struct Rv64ProgramContext {
    const char *path;
    MinicDiagnostic *diagnostic;
} Rv64ProgramContext;

static bool rv64_visit_global(
    FILE *file, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *object, size_t index, void *opaque) {
    Rv64ProgramContext *ctx = opaque;
    (void)target;
    if (object->is_extern) {
        if (object->alias_target != MINIC_GLOBAL_OBJECT_INVALID)
            return minic_multi_elf_emit_object_alias(
                file, program, object, "@object");
        return !object->is_weak || object->is_internal ||
               fprintf(file, ".weak %s\n", object->name) >= 0;
    }
    if (minic_riscv64_emit_global_object(file, program, object)) return true;
    {
        char message[256];
        const MinicGlobalRelocation *first =
            object->relocation_count != 0U ? &object->relocations[0] : NULL;
        (void)snprintf(message, sizeof(message),
                       "cannot emit RISC-V global object '%s' (index=%zu init=%zu reloc=%zu "
                       "union=%zu first-kind=%u first-slot=%zu)",
                       object->name, index, object->initializer_count,
                       object->relocation_count, object->union_selection_count,
                       first != NULL ? (unsigned int)first->location_kind : 999U,
                       first != NULL ? first->location_index : 0U);
        minic_riscv64_set_diagnostic(ctx->diagnostic, ctx->path, message);
    }
    return false;
}

static bool rv64_visit_function(
    FILE *file, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicFunction *function, const MinicCoreFunction *core_function,
    const MinicCoreFunctionFacts *facts,
    const MinicCoreObjectStoragePlan *storage, size_t index,
    bool function_sections, void *opaque) {
    Rv64ProgramContext *ctx = opaque;
    bool success = true;
    if (getenv("MINIC_BOOTSTRAP_TRACE") != NULL && function->is_defined) {
        const char *name = minic_c0_function_symbol_name(function);
        (void)fprintf(stderr,
            "MINIC_BOOTSTRAP_TRACE stage=codegen-function state=begin "
            "index=%zu function=%s\n",
            index, name != NULL ? name : "?");
        (void)fflush(stderr);
    }
    if (!function->is_defined) {
        const char *name = minic_c0_function_symbol_name(function);
        if (function->alias_target != MINIC_FUNCTION_INVALID)
            return minic_riscv_emit_function_alias(file, program, function);
        if (function->is_weak && !function->is_internal)
            return name != NULL && name[0] != '\0' &&
                   fprintf(file, ".weak %s\n", name) >= 0;
        return true;
    }
    {
        MinicRiscvFunctionSymbol symbol;
        /* The machine emitter owns capability admission, not this visitor. */
        success = minic_riscv_function_symbol_from_function(function, &symbol);
        symbol.function_sections = function_sections;
        success = success && minic_riscv64_emit_core_function_for_program_with_symbol_with_storage(
                      file, program, core_function, &symbol, facts, storage,
                      target != NULL && target->position_independent,
                      target != NULL ? &target->riscv_isa : NULL,
                      ctx->diagnostic);
        if (success && function->is_constructor) {
            const char *name = minic_c0_function_symbol_name(function);
            success = name != NULL && name[0] != '\0' &&
                      fprintf(file, ".section .init_array,\"aw\",@init_array\n"
                                    ".p2align 3\n  .dword %s\n.text\n", name) >= 0;
        }
    }
    if (getenv("MINIC_BOOTSTRAP_TRACE") != NULL) {
        const char *name = minic_c0_function_symbol_name(function);
        (void)fprintf(stderr,
            "MINIC_BOOTSTRAP_TRACE stage=codegen-function state=end "
            "index=%zu function=%s success=%d blocks=%zu instructions=%zu\n",
            index, name != NULL ? name : "?", success ? 1 : 0,
            core_function->block_count, core_function->instruction_count);
        (void)fflush(stderr);
    }
    if (!success &&
        ctx->diagnostic != NULL && ctx->diagnostic->message[0] == '\0') {
        char message[256];
        const char *name = minic_c0_function_symbol_name(function);
        (void)snprintf(message, sizeof(message),
                       "cannot emit RISC-V function '%s' (index=%zu)",
                       name != NULL ? name : "<unnamed>", index);
        minic_riscv64_set_diagnostic(ctx->diagnostic, ctx->path, message);
    }
    return success;
}

static bool rv64_between(FILE *file, const MinicC0Program *program,
                         const MinicTargetInfo *target,
                         const MinicCoreFunction *functions, size_t count,
                         bool function_sections, void *opaque) {
    size_t i;
    (void)target;
    (void)functions;
    (void)count;
    (void)function_sections;
    (void)opaque;
    if (program->file_asm_count != 0U) {
        if (fprintf(file, ".text\n") < 0) return false;
        for (i = 0U; i < program->file_asm_count; ++i) {
            const MinicFileAsm *source = &program->file_asms[i];
            if (!minic_riscv64_source_asm_marker_safe(source->text, source->length) ||
                !minic_riscv64_asm_audit_marker(file, true) ||
                !minic_riscv64_emit_file_asm(file, source) ||
                fputc('\n', file) == EOF ||
                !minic_riscv64_asm_audit_marker(file, false))
                return false;
        }
    }
    return fprintf(file, ".text\n") >= 0;
}

static bool rv64_finalize(const char *path, const MinicTargetInfo *target,
                          void *opaque) {
    Rv64ProgramContext *ctx = opaque;
    char isa_error[240];
    if (target == NULL || !target->riscv_isa.strict_isa) return true;
    if (minic_riscv64_check_isa_assembly(
            path, &target->riscv_isa, isa_error, sizeof(isa_error)))
        return true;
    minic_riscv64_set_diagnostic(ctx->diagnostic, path, isa_error);
    return false;
}

bool minic_riscv64_write_c0_program_with_core_functions(const char *path,
                                                        const MinicC0Program *program,
                                                        const MinicCoreFunction *core_functions,
                                                        size_t core_function_count,
                                                        bool function_sections,
                                                        const MinicTargetInfo *target,
                                                        MinicDiagnostic *diagnostic) {
    static const MinicProgramEmitConfig config = {
        rv64_visit_global, rv64_visit_function, NULL, rv64_between,
        NULL, rv64_finalize, MINIC_PROGRAM_FUNCTION_ALL, false, false
    };
    Rv64ProgramContext context = { path, diagnostic };
    bool success;
    if (program == NULL || target == NULL || path == NULL) {
        minic_riscv64_set_diagnostic(diagnostic, path, "program/target/path is required");
        return false;
    }
    if (core_function_count != program->function_count ||
        (core_function_count != 0U && core_functions == NULL)) {
        minic_riscv64_set_diagnostic(diagnostic, path,
            "Core function map does not match program functions");
        return false;
    }
    /* GNU source-asm provenance and strict-I audits remain target-owned.
     * The common transaction removes partial output in ALL ISA profiles. */
    success = minic_program_emit_write(
        path, program, target, core_functions, core_function_count,
        function_sections, &config, &context);
    if (!success && diagnostic != NULL && diagnostic->message[0] == '\0')
        minic_riscv64_set_diagnostic(diagnostic, path, "cannot write RISC-V assembly");
    return success;
}
