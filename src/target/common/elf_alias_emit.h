#ifndef MINIC_TARGET_COMMON_ELF_ALIAS_EMIT_H
#define MINIC_TARGET_COMMON_ELF_ALIAS_EMIT_H

#include "frontend/ast.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* ELF alias semantics are common; only @function versus %function (and
 * object equivalent) is a GNU assembler dialect choice. No physical
 * ABI/register rules belong here. Keep RV32/RV64's qualified emitters
 * unchanged until their full Linux Image/P1 gates can be repeated. */
static inline const char *minic_multi_elf_alias_visibility(
    MinicSymbolVisibility visibility) {
    switch (visibility) {
    case MINIC_SYMBOL_VISIBILITY_DEFAULT: return NULL;
    case MINIC_SYMBOL_VISIBILITY_HIDDEN: return ".hidden";
    case MINIC_SYMBOL_VISIBILITY_INTERNAL: return ".internal";
    case MINIC_SYMBOL_VISIBILITY_PROTECTED: return ".protected";
    default: return NULL;
    }
}

static inline bool minic_multi_elf_emit_alias(
    FILE *out, const char *alias_name, const char *target_name,
    bool internal, bool weak, MinicSymbolVisibility visibility,
    const char *type_directive) {
    const char *directive = minic_multi_elf_alias_visibility(visibility);
    if (out == NULL || alias_name == NULL || target_name == NULL ||
        type_directive == NULL || alias_name[0] == '\0' ||
        target_name[0] == '\0' || strcmp(alias_name, target_name) == 0 ||
        (weak && internal) ||
        (visibility != MINIC_SYMBOL_VISIBILITY_DEFAULT && directive == NULL))
        return false;
    if (!internal &&
        fprintf(out, weak ? ".weak %s\n" : ".globl %s\n", alias_name) < 0)
        return false;
    if (!internal && directive != NULL &&
        fprintf(out, "%s %s\n", directive, alias_name) < 0)
        return false;
    return fprintf(out, ".type %s, %s\n.set %s, %s\n",
                   alias_name, type_directive, alias_name, target_name) >= 0;
}

/* The parser and AST verifier already require a defined, representation-
 * compatible same-TU target.  The GNU object-alias boundary also allows
 * cv-qualified array elements to name the same storage; ordinary C type
 * compatibility elsewhere still remains strict. */
static inline bool minic_multi_elf_emit_object_alias(
    FILE *out, const MinicC0Program *program,
    const MinicGlobalObject *alias, const char *type_directive) {
    const MinicGlobalObject *target;
    if (program == NULL || alias == NULL || !alias->is_extern ||
        alias->alias_target == MINIC_GLOBAL_OBJECT_INVALID ||
        alias->alias_target >= program->global_object_count)
        return false;
    target = minic_c0_program_global_object(program, alias->alias_target);
    return target != NULL && target != alias &&
           !target->is_extern && !target->is_tentative &&
           minic_c0_object_alias_types_compatible(program, alias->type, target->type) &&
           minic_multi_elf_emit_alias(
               out, alias->name, target->name, alias->is_internal,
               alias->is_weak, alias->visibility, type_directive);
}

/* The program visitor emits only live function definitions for x86/Direct
 * LP64. This common declaration stage is the single owner of their aliases;
 * don't make the machine function emitters fabricate dummy Core bodies. */
static inline bool minic_multi_elf_emit_function_aliases(
    FILE *out, const MinicC0Program *program, const char *type_directive) {
    size_t index;
    if (out == NULL || program == NULL || type_directive == NULL)
        return false;
    for (index = 0U; index < program->function_count; ++index) {
        const MinicFunction *alias = &program->functions[index];
        const MinicFunction *target;
        if (alias->alias_target == MINIC_FUNCTION_INVALID)
            continue;
        if (alias->is_defined ||
            alias->alias_target >= program->function_count)
            return false;
        target = minic_c0_program_function(program, alias->alias_target);
        if (target == NULL || !target->is_defined ||
            target->alias_target != MINIC_FUNCTION_INVALID ||
            !minic_multi_elf_emit_alias(
                out, minic_c0_function_symbol_name(alias),
                minic_c0_function_symbol_name(target), alias->is_internal,
                alias->is_weak, alias->visibility, type_directive))
            return false;
    }
    return true;
}

#endif
