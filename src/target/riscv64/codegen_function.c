#include "target/riscv64/codegen.h"
#include "target/riscv64/global_emit.h"
#include "target/common/global_emit.h"
#include "target/riscv64/abi.h"
#include "target/riscv64/codegen_internal.h"
#include "target/riscv64/core_codegen.h"
#include "target/riscv64/function_symbol.h"
#include "target/data_layout.h"
#include "target/riscv64/isa.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool minic_riscv64_write_c0_program_with_core_functions(const char *path,
                                                        const MinicC0Program *program,
                                                        const MinicCoreFunction *core_functions,
                                                        size_t core_function_count,
                                                        bool function_sections,
                                                        const MinicTargetInfo *target,
                                                        MinicDiagnostic *diagnostic) {
    FILE *file;
    size_t global_index;
    size_t function_index;
    bool success;

    if (program == NULL) {
        minic_riscv64_set_diagnostic(diagnostic, path, "program is required");
        return false;
    }
    if (core_function_count != program->function_count ||
        (core_function_count != 0U && core_functions == NULL)) {
        minic_riscv64_set_diagnostic(
            diagnostic, path, "Core function map does not match program functions");
        return false;
    }

    /* Source-origin GNU asm is validated for operand transport by Core,
     * then forwarded to the assembler. ISA restrictions on compiler-emitted
     * code must not reject an upstream Linux CSR/A/M/privileged asm template.
     * The final RISC-V ISA audit only inspects code outside source markers. */

    /* LP64 uses two XLEN slots for C __int128 parameters and returns.
     * Unsupported 128-bit ARITHMETIC remains rejected by Core ISA
     * legalization, rather than rejecting otherwise ABI-valid prototypes. */

    /* Full LP64 integer-register psABI: float/double arguments and returns
     * travel in GPRs, and records use XLEN-word chunks/indirect storage.
     * Distinguish this psABI from the separate software-arithmetic library.
     * 128-bit non-qualified arithmetic remains separately diagnosed by the
     * instruction legalizer rather than restricting valid ABI signatures. */

    file = fopen(path, "wb");
    if (file == NULL) {
        char message[256];

        (void)snprintf(message, sizeof(message), "cannot open output: %s", strerror(errno));
        minic_riscv64_set_diagnostic(diagnostic, path, message);
        return false;
    }

    success = true;
    for (global_index = 0U; success && global_index < program->global_object_count;
         ++global_index) {
        if (program->global_objects[global_index].is_extern) {
            const MinicGlobalObject *object;

            object = &program->global_objects[global_index];
            if (object->alias_target != MINIC_GLOBAL_OBJECT_INVALID) {
                const MinicGlobalObject *target_object;
                const char *visibility_directive;

                target_object = minic_c0_program_global_object(program, object->alias_target);
                success = target_object != NULL && target_object != object &&
                          !target_object->is_extern && !target_object->is_tentative &&
                          minic_c0_types_compatible(program, object->type, target_object->type);
                if (success && !object->is_internal) {
                    success = fprintf(file,
                                      object->is_weak ? ".weak %s\n" : ".globl %s\n",
                                      object->name) >= 0;
                    if (success && object->visibility != MINIC_SYMBOL_VISIBILITY_DEFAULT) {
                        visibility_directive =
                            minic_multi_elf_visibility_directive(object->visibility);
                        success = visibility_directive != NULL &&
                                  fprintf(file, "%s %s\n", visibility_directive, object->name) >= 0;
                    }
                }
                if (success) {
                    success = fprintf(file,
                                      ".type %s, @object\n"
                                      ".set %s, %s\n",
                                      object->name,
                                      object->name,
                                      target_object->name) >= 0;
                }
                continue;
            }
            if (object->is_weak && !object->is_internal &&
                fprintf(file, ".weak %s\n", object->name) < 0) {
                success = false;
            }
            continue;
        }
        if (!minic_riscv64_emit_global_object(
                file, program, &program->global_objects[global_index])) {
            char message[256];
            const MinicGlobalObject *object;

            object = &program->global_objects[global_index];
            {
                const MinicGlobalRelocation *first_relocation;

                first_relocation = object->relocation_count != 0U ? &object->relocations[0] : NULL;
                (void)snprintf(
                    message,
                    sizeof(message),
                    "cannot emit RISC-V global object '%s' (index=%zu init=%zu reloc=%zu "
                    "union=%zu first-kind=%u first-slot=%zu)",
                    object->name,
                    global_index,
                    object->initializer_count,
                    object->relocation_count,
                    object->union_selection_count,
                    first_relocation != NULL ? (unsigned int)first_relocation->location_kind : 999U,
                    first_relocation != NULL ? first_relocation->location_index : 0U);
            }
            minic_riscv64_set_diagnostic(diagnostic, path, message);
            success = false;
        }
    }
    if (success && program->file_asm_count != 0U) {
        size_t file_asm_index;

        success = fprintf(file, ".text\n") >= 0;
        for (file_asm_index = 0U; success && file_asm_index < program->file_asm_count;
             ++file_asm_index) {
            success = minic_riscv64_asm_audit_marker(file, true) &&
                      minic_riscv64_emit_file_asm(file, &program->file_asms[file_asm_index]);
            /* Keep the end-marker on its own physical source line. */
            if (success) success = fputc('\n', file) != EOF;
            if (success) success = minic_riscv64_asm_audit_marker(file, false);
        }
    }
    if (success) {
        success = fprintf(file, ".text\n") >= 0;
    }

    for (function_index = 0U; success && function_index < program->function_count;
         ++function_index) {
        const MinicFunction *function;
        const MinicCoreFunction *core_function;

        function = &program->functions[function_index];
        if (function->is_defined && function->is_internal && !function->is_referenced) {
            continue;
        }
        if (getenv("MINIC_BOOTSTRAP_TRACE") != NULL && function->is_defined) {
            const char *trace_symbol = minic_c0_function_symbol_name(function);
            (void)fprintf(stderr,
                          "MINIC_BOOTSTRAP_TRACE stage=codegen-function state=begin "
                          "index=%zu function=%s\n",
                          function_index,
                          trace_symbol != NULL ? trace_symbol : "?");
            (void)fflush(stderr);
        }
        if (!function->is_defined) {
            const char *symbol_name;

            symbol_name = minic_c0_function_symbol_name(function);
            if (function->alias_target != MINIC_FUNCTION_INVALID) {
                success = minic_riscv_emit_function_alias(file, program, function);
                continue;
            }
            if (function->is_weak && !function->is_internal) {
                success = symbol_name != NULL && symbol_name[0] != '\0' &&
                          fprintf(file, ".weak %s\n", symbol_name) >= 0;
            }
            continue;
        }
        core_function = &core_functions[function_index];
        {
            MinicRiscv64FunctionSymbol symbol;

            /* emit_core_function_for_program_with_symbol() owns the complete
               capability check.  Do not preflight the same Core function here:
               on self-hosted giant functions that duplicated verification was
               a material part of codegen time and provided no extra safety. */
            success = minic_riscv64_function_symbol_from_function(function, &symbol);
            symbol.function_sections = function_sections;
            success = success && minic_riscv64_emit_core_function_for_program_with_symbol(
                          file, program, core_function, &symbol,
                          target != NULL && target->position_independent,
                          target != NULL ? &target->riscv_isa : NULL, diagnostic);
            /* GNU constructor is a real ELF init action on RV64 as well:
             * retain its address as an init_array relocation, never discard. */
            if (success && function->is_constructor) {
                const char *init_symbol = minic_c0_function_symbol_name(function);
                success = init_symbol != NULL && init_symbol[0] != '\0' &&
                          fprintf(file, ".section .init_array,\"aw\",@init_array\n"
                                        ".p2align 3\n  .dword %s\n.text\n",
                                  init_symbol) >= 0;
            }
        }
        if (getenv("MINIC_BOOTSTRAP_TRACE") != NULL && function->is_defined) {
            const char *trace_symbol = minic_c0_function_symbol_name(function);
            (void)fprintf(stderr,
                          "MINIC_BOOTSTRAP_TRACE stage=codegen-function state=end "
                          "index=%zu function=%s success=%d blocks=%zu instructions=%zu\n",
                          function_index,
                          trace_symbol != NULL ? trace_symbol : "?",
                          success ? 1 : 0,
                          core_function != NULL ? core_function->block_count : 0U,
                          core_function != NULL ? core_function->instruction_count : 0U);
            (void)fflush(stderr);
        }
        if (!success && diagnostic != NULL && diagnostic->message[0] == '\0') {
            char message[256];
            const char *symbol_name;

            symbol_name = minic_c0_function_symbol_name(function);
            (void)snprintf(message,
                           sizeof(message),
                           "cannot emit RISC-V function '%s' (index=%zu)",
                           symbol_name != NULL ? symbol_name : "<unnamed>",
                           function_index);
            minic_riscv64_set_diagnostic(diagnostic, path, message);
        }
    }

    if (!success && (diagnostic == NULL || diagnostic->message[0] == '\0')) {
        minic_riscv64_set_diagnostic(diagnostic, path, "cannot write RISC-V assembly");
    }
    if (fclose(file) != 0 && success) {
        minic_riscv64_set_diagnostic(diagnostic, path, "cannot close RISC-V assembly output");
        success = false;
    }
    /* Strict RV64I audits ACTUAL machine output from every emission family.
     * Legacy RV64 Linux/selfhost output has zero additional work. */
    if (success && target != NULL && target->riscv_isa.strict_isa) {
        char isa_error[240];
        if (!minic_riscv64_check_isa_assembly(path, &target->riscv_isa, isa_error, sizeof(isa_error))) {
            minic_riscv64_set_diagnostic(diagnostic, path, isa_error);
            (void)remove(path);
            return false;
        }
    }
    if (!success && target != NULL && target->riscv_isa.strict_isa)
        (void)remove(path);
    return success;
}
