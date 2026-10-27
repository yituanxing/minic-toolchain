#ifndef MINIC_TARGET_RISCV_FUNCTION_SYMBOL_H
#define MINIC_TARGET_RISCV_FUNCTION_SYMBOL_H

#include "frontend/ast.h"

#include <stdbool.h>
#include <stdio.h>

typedef struct MinicRiscvFunctionSymbol {
    const char *symbol_name;
    const char *section_name;
    MinicSymbolVisibility visibility;
    bool is_internal;
    bool is_weak;
    bool function_sections;
} MinicRiscvFunctionSymbol;

static inline bool minic_riscv_function_symbol_from_function(const MinicFunction *function,
                                                               MinicRiscvFunctionSymbol *symbol) {
    const char *symbol_name;

    if (function == NULL || symbol == NULL) {
        return false;
    }
    symbol_name = minic_c0_function_symbol_name(function);
    if (symbol_name == NULL || symbol_name[0] == '\0') {
        return false;
    }
    symbol->symbol_name = symbol_name;
    symbol->section_name = function->section_name;
    symbol->visibility = function->visibility;
    symbol->is_internal = function->is_internal;
    symbol->is_weak = function->is_weak;
    symbol->function_sections = false;
    return true;
}

static inline const char *
minic_riscv_function_visibility_directive(MinicSymbolVisibility visibility) {
    return visibility == MINIC_SYMBOL_VISIBILITY_HIDDEN      ? ".hidden"
           : visibility == MINIC_SYMBOL_VISIBILITY_INTERNAL  ? ".internal"
           : visibility == MINIC_SYMBOL_VISIBILITY_PROTECTED ? ".protected"
                                                             : NULL;
}

/* GNU function aliases have identical ELF semantics on RV32 and RV64.
 * Keep the shared binding/type/visibility contract at the RISC-V symbol
 * boundary so a target cannot silently omit a live weak alias. */
static inline bool minic_riscv_emit_function_alias(FILE *file,
                                                  const MinicC0Program *program,
                                                  const MinicFunction *function) {
    const MinicFunction *target;
    const char *alias_name;
    const char *target_name;
    const char *visibility_directive;

    if (file == NULL || program == NULL || function == NULL ||
        function->alias_target == MINIC_FUNCTION_INVALID) {
        return false;
    }
    target = minic_c0_program_function(program, function->alias_target);
    alias_name = minic_c0_function_symbol_name(function);
    target_name = minic_c0_function_symbol_name(target);
    if (alias_name == NULL || alias_name[0] == '\0' || target == NULL ||
        !target->is_defined || target_name == NULL || target_name[0] == '\0') {
        return false;
    }
    if (!function->is_internal) {
        if (fprintf(file, function->is_weak ? ".weak %s\n" : ".globl %s\n",
                    alias_name) < 0) {
            return false;
        }
        if (function->visibility != MINIC_SYMBOL_VISIBILITY_DEFAULT) {
            visibility_directive =
                minic_riscv_function_visibility_directive(function->visibility);
            if (visibility_directive == NULL ||
                fprintf(file, "%s %s\n", visibility_directive, alias_name) < 0) {
                return false;
            }
        }
    }
    return fprintf(file, ".type %s, @function\n.set %s, %s\n",
                   alias_name, alias_name, target_name) >= 0;
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
