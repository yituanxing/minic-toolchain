#ifndef MINIC_TARGET_RISCV_FUNCTION_SYMBOL_H
#define MINIC_TARGET_RISCV_FUNCTION_SYMBOL_H

#include "core/module_ir.h"
#include "target/common/elf_alias_emit.h"

#include <stdbool.h>
#include <stdio.h>

typedef struct MinicRiscvFunctionSymbol {
    const char *symbol_name;
    const char *section_name;
    size_t explicit_alignment;
    MinicCoreModuleVisibility visibility;
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
    symbol->visibility = MINIC_CORE_MODULE_VISIBILITY_DEFAULT;
    return true;
}

/* Production sealed ModuleIR descriptor. Never reopen C declarations for
 * symbol identity, visibility, section, alignment or linkage attributes. */
static inline bool minic_riscv_function_symbol_from_module(
    const MinicCoreModuleFunctionSymbol *function,
    MinicRiscvFunctionSymbol *symbol) {
    if (function == NULL || symbol == NULL ||
        !minic_riscv_function_symbol_for_name(function->name, symbol)) return false;
    symbol->section_name = function->section_name;
    symbol->explicit_alignment = function->explicit_alignment;
    symbol->visibility = function->visibility;
    symbol->is_internal = function->is_internal;
    symbol->is_weak = function->is_weak;
    return true;
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
        if (symbol->visibility != MINIC_CORE_MODULE_VISIBILITY_DEFAULT) {
            visibility_directive = minic_multi_elf_module_visibility_directive(
                symbol->visibility);
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
