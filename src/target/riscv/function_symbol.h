#ifndef MINIC_TARGET_RISCV_FUNCTION_SYMBOL_H
#define MINIC_TARGET_RISCV_FUNCTION_SYMBOL_H

#include "frontend/ast.h"
#include "target/common/elf_alias_emit.h"

#include <stdbool.h>
#include <stdio.h>

typedef struct MinicRiscvFunctionSymbol {
    const char *symbol_name;
    const char *section_name;
    size_t explicit_alignment;
    MinicSymbolVisibility visibility;
    bool is_internal;
    bool is_weak;
    bool function_sections;
} MinicRiscvFunctionSymbol;

/* A complete symbolic function descriptor has an explicit zero-alignment
 * default. Standalone Core compatibility entry points cannot leave newly
 * added symbol metadata uninitialized when they only know a name. */
static inline bool minic_riscv_function_symbol_for_name(
    const char *name, MinicRiscvFunctionSymbol *symbol) {
    if (name == NULL || name[0] == '\0' || symbol == NULL) return false;
    *symbol = (MinicRiscvFunctionSymbol){0};
    symbol->symbol_name = name;
    symbol->visibility = MINIC_SYMBOL_VISIBILITY_DEFAULT;
    return true;
}

static inline bool minic_riscv_function_symbol_from_function(const MinicFunction *function,
                                                               MinicRiscvFunctionSymbol *symbol) {
    const char *symbol_name;

    if (function == NULL || symbol == NULL) {
        return false;
    }
    symbol_name = minic_c0_function_symbol_name(function);
    if (!minic_riscv_function_symbol_for_name(symbol_name, symbol)) {
        return false;
    }
    symbol->section_name = function->section_name;
    symbol->explicit_alignment = function->explicit_alignment;
    symbol->visibility = function->visibility;
    symbol->is_internal = function->is_internal;
    symbol->is_weak = function->is_weak;
    return true;
}

static inline const char *
minic_riscv_function_visibility_directive(MinicSymbolVisibility visibility) {
    return visibility == MINIC_SYMBOL_VISIBILITY_HIDDEN      ? ".hidden"
           : visibility == MINIC_SYMBOL_VISIBILITY_INTERNAL  ? ".internal"
           : visibility == MINIC_SYMBOL_VISIBILITY_PROTECTED ? ".protected"
                                                             : NULL;
}

/* RISC-V shares the exact GNU ELF alias semantics with x86/Direct LP64.
 * Keep the ABI and ISA emission owned by the target; this is symbol
 * metadata only, no new machine instruction or object format. */
static inline bool minic_riscv_emit_function_alias(
    FILE *file, const MinicC0Program *program,
    const MinicFunction *function) {
    const MinicFunction *owner;
    if (file == NULL || program == NULL || function == NULL ||
        function->alias_target == MINIC_FUNCTION_INVALID)
        return false;
    owner = minic_c0_program_function(program, function->alias_target);
    return owner != NULL && owner->is_defined &&
           minic_multi_elf_emit_alias(
               file, minic_c0_function_symbol_name(function),
               minic_c0_function_symbol_name(owner), function->is_internal,
               function->is_weak, function->visibility, "@function");
}

static inline bool
minic_riscv_emit_function_symbol_begin(FILE *file, const MinicRiscvFunctionSymbol *symbol) {
    const char *visibility_directive;

    if (file == NULL || symbol == NULL || symbol->symbol_name == NULL ||
        symbol->symbol_name[0] == '\0') {
        return false;
    }
    if (symbol->section_name != NULL) {
        if (fprintf(file, ".section %s,\"ax\",@progbits\n", symbol->section_name) < 0) {
            return false;
        }
    } else if (symbol->function_sections) {
        if (fprintf(file, ".section .text.%s,\"ax\",@progbits\n", symbol->symbol_name) < 0) {
            return false;
        }
    } else if (fprintf(file, ".text\n") < 0) {
        return false;

    }
    if (!symbol->is_internal) {
        if (fprintf(file, symbol->is_weak ? ".weak %s\n" : ".globl %s\n", symbol->symbol_name) <
            0) {
            return false;
        }
        if (symbol->visibility != MINIC_SYMBOL_VISIBILITY_DEFAULT) {
            visibility_directive = minic_riscv_function_visibility_directive(symbol->visibility);
            if (visibility_directive == NULL ||
                fprintf(file, "%s %s\n", visibility_directive, symbol->symbol_name) < 0) {
                return false;
            }
        }
    }
    if (symbol->explicit_alignment != 0U &&
        fprintf(file, ".balign %zu\n", symbol->explicit_alignment) < 0)
        return false;
    return fprintf(file,
                   ".type %s, @function\n"
                   "%s:\n",
                   symbol->symbol_name,
                   symbol->symbol_name) >= 0;
}

static inline bool
minic_riscv_emit_function_symbol_end(FILE *file, const MinicRiscvFunctionSymbol *symbol) {
    return file != NULL && symbol != NULL && symbol->symbol_name != NULL &&
           symbol->symbol_name[0] != '\0' &&
           fprintf(file, ".size %s, .-%s\n", symbol->symbol_name, symbol->symbol_name) >= 0;
}

#endif
