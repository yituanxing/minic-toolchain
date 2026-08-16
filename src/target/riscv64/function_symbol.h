#ifndef MINIC_TARGET_RISCV64_FUNCTION_SYMBOL_H
#define MINIC_TARGET_RISCV64_FUNCTION_SYMBOL_H

#include "frontend/ast.h"

#include <stdbool.h>
#include <stdio.h>

typedef struct MinicRiscv64FunctionSymbol {
    const char *symbol_name;
    const char *section_name;
    MinicSymbolVisibility visibility;
    bool is_internal;
    bool is_weak;
} MinicRiscv64FunctionSymbol;

static inline bool minic_riscv64_function_symbol_from_function(const MinicFunction *function,
                                                               MinicRiscv64FunctionSymbol *symbol) {
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
    return true;
}

static inline const char *
minic_riscv64_function_visibility_directive(MinicSymbolVisibility visibility) {
    return visibility == MINIC_SYMBOL_VISIBILITY_HIDDEN      ? ".hidden"
           : visibility == MINIC_SYMBOL_VISIBILITY_INTERNAL  ? ".internal"
           : visibility == MINIC_SYMBOL_VISIBILITY_PROTECTED ? ".protected"
                                                             : NULL;
}

static inline bool
minic_riscv64_emit_function_symbol_begin(FILE *file, const MinicRiscv64FunctionSymbol *symbol) {
    const char *visibility_directive;

    if (file == NULL || symbol == NULL || symbol->symbol_name == NULL ||
        symbol->symbol_name[0] == '\0') {
        return false;
    }
    if (symbol->section_name != NULL) {
        if (fprintf(file, ".section %s\n", symbol->section_name) < 0) {
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
            visibility_directive = minic_riscv64_function_visibility_directive(symbol->visibility);
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
minic_riscv64_emit_function_symbol_end(FILE *file, const MinicRiscv64FunctionSymbol *symbol) {
    return file != NULL && symbol != NULL && symbol->symbol_name != NULL &&
           symbol->symbol_name[0] != '\0' &&
           fprintf(file, ".size %s, .-%s\n", symbol->symbol_name, symbol->symbol_name) >= 0;
}

#endif
