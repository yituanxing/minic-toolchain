#ifndef MINIC_TARGET_COMMON_ELF_ALIAS_EMIT_H
#define MINIC_TARGET_COMMON_ELF_ALIAS_EMIT_H

#include "core/module_ir.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* ELF alias semantics are common; only @function versus %function (and
 * object equivalent) is a GNU assembler dialect choice. No physical
 * ABI/register rules belong here. Keep RV32/RV64's qualified emitters
 * unchanged until their full Linux Image/P1 gates can be repeated. */
static inline bool minic_multi_elf_emit_alias_directive(
    FILE *out, const char *alias_name, const char *target_name,
    bool internal, bool weak, const char *visibility_directive,
    const char *type_directive) {
    if (out == NULL || alias_name == NULL || target_name == NULL ||
        type_directive == NULL || alias_name[0] == '\0' ||
        target_name[0] == '\0' || strcmp(alias_name, target_name) == 0 ||
        (weak && internal))
        return false;
    if (!internal &&
        fprintf(out, weak ? ".weak %s\n" : ".globl %s\n", alias_name) < 0)
        return false;
    if (!internal && visibility_directive != NULL &&
        fprintf(out, "%s %s\n", visibility_directive, alias_name) < 0)
        return false;
    return fprintf(out, ".type %s, %s\n.set %s, %s\n",
                   alias_name, type_directive, alias_name, target_name) >= 0;
}

static inline const char *minic_multi_elf_module_visibility_directive(
    MinicCoreModuleVisibility visibility) {
    switch (visibility) {
    case MINIC_CORE_MODULE_VISIBILITY_DEFAULT: return NULL;
    case MINIC_CORE_MODULE_VISIBILITY_HIDDEN: return ".hidden";
    case MINIC_CORE_MODULE_VISIBILITY_INTERNAL: return ".internal";
    case MINIC_CORE_MODULE_VISIBILITY_PROTECTED: return ".protected";
    }
    return NULL;
}

static inline bool minic_multi_elf_emit_module_alias(
    FILE *out, const char *alias_name, const char *target_name,
    bool internal, bool weak, MinicCoreModuleVisibility visibility,
    const char *type_directive) {
    const char *directive = minic_multi_elf_module_visibility_directive(visibility);
    return (visibility == MINIC_CORE_MODULE_VISIBILITY_DEFAULT || directive != NULL) &&
           minic_multi_elf_emit_alias_directive(
               out, alias_name, target_name, internal, weak, directive,
               type_directive);
}

/* Compatibility of the two C declarations was already proved at the
 * source-to-module boundary. Machine emission only checks the sealed alias
 * identity and the ELF target's definition status. */
static inline bool minic_multi_elf_emit_module_object_alias(
    FILE *out, const MinicCoreModule *module,
    const MinicCoreModuleGlobal *alias, const char *type_directive) {
    const MinicCoreModuleGlobal *target;
    if (module == NULL || !module->finalized || alias == NULL ||
        module->globals == NULL || !alias->is_extern || !alias->is_alias ||
        alias->alias_target_index >= module->global_count)
        return false;
    target = &module->globals[alias->alias_target_index];
    return target != alias && !target->is_extern && !target->is_tentative &&
           !target->is_alias && target->name != NULL &&
           alias->alias_target_name != NULL &&
           strcmp(target->name, alias->alias_target_name) == 0 &&
           minic_multi_elf_emit_module_alias(
               out, alias->name, target->name, alias->is_internal,
               alias->is_weak, alias->visibility, type_directive);
}

static inline bool minic_multi_elf_emit_module_function_alias(
    FILE *out, const MinicCoreModule *module, size_t index,
    const char *type_directive) {
    const MinicCoreModuleFunctionSymbol *alias;
    const MinicCoreModuleFunctionSymbol *target;
    if (out == NULL || module == NULL || !module->finalized ||
        type_directive == NULL ||
        index >= module->function_count || module->function_symbols == NULL)
        return false;
    alias = &module->function_symbols[index];
    if (alias->alias_target_index == SIZE_MAX) return true;
    if (alias->is_defined || alias->alias_target_index >= module->function_count)
        return false;
    target = &module->function_symbols[alias->alias_target_index];
    return target != alias && target->is_defined &&
           target->alias_target_index == SIZE_MAX &&
           target->name != NULL && alias->alias_target_name != NULL &&
           strcmp(alias->alias_target_name, target->name) == 0 &&
           minic_multi_elf_emit_module_alias(
               out, alias->name, target->name, alias->is_internal,
               alias->is_weak, alias->visibility, type_directive);
}

static inline bool minic_multi_elf_emit_module_function_aliases(
    FILE *out, const MinicCoreModule *module, const char *type_directive) {
    size_t index;
    if (out == NULL || module == NULL || !module->finalized ||
        type_directive == NULL ||
        (module->function_count != 0U && module->function_symbols == NULL))
        return false;
    for (index = 0U; index < module->function_count; ++index)
        if (!minic_multi_elf_emit_module_function_alias(
                out, module, index, type_directive)) return false;
    return true;
}

#endif
