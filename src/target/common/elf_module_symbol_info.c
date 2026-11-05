#include "target/common/elf_module_symbol_info.h"

#include <string.h>

static bool elf_visibility_nonpreemptible(MinicCoreModuleVisibility visibility) {
    return visibility == MINIC_CORE_MODULE_VISIBILITY_HIDDEN ||
           visibility == MINIC_CORE_MODULE_VISIBILITY_INTERNAL;
}

MinicElfModuleSymbolFacts minic_multi_elf_module_symbol_facts(
    const MinicCoreModule *module, const char *symbol,
    MinicElfModuleSymbolKind kind) {
    MinicElfModuleSymbolFacts facts = {0};
    bool defined = false;
    size_t index;

    if (module == NULL || !module->finalized || symbol == NULL ||
        (module->global_count != 0U && module->globals == NULL) ||
        (module->function_count != 0U && module->function_symbols == NULL) ||
        (kind != MINIC_ELF_MODULE_SYMBOL_OBJECT &&
         kind != MINIC_ELF_MODULE_SYMBOL_FUNCTION))
        return facts;
    /* GNU/ELF has ONE physical assembler-symbol namespace. An object-typed
     * source declaration may resolve to a function owner and vice versa;
     * instruction use kind must never resurrect a weak undefined binding
     * for the exact spelling that a different Core entity emits strongly. */
    for (index = 0U; index < module->global_count; ++index) {
        const MinicCoreModuleGlobal *object = &module->globals[index];
        bool local, owner;
        if (object->name == NULL || strcmp(object->name, symbol) != 0)
            continue;
        local = object->emittable && object->is_internal &&
                (object->definition_kind != MINIC_CORE_MODULE_DEFINITION_DECLARATION ||
                 object->alias_target_index != SIZE_MAX);
        owner = object->emittable &&
                (object->definition_kind != MINIC_CORE_MODULE_DEFINITION_DECLARATION ||
                 object->alias_target_index != SIZE_MAX);
        facts.tu_local |= local;
        facts.nonpreemptible |= local ||
            ((object->emittable ||
              object->definition_kind == MINIC_CORE_MODULE_DEFINITION_DECLARATION) &&
             elf_visibility_nonpreemptible(object->visibility));
        facts.undefined_weak |= object->is_weak &&
            object->definition_kind == MINIC_CORE_MODULE_DEFINITION_DECLARATION;
        defined |= owner;
    }
    for (index = 0U; index < module->function_count; ++index) {
        const MinicCoreModuleFunctionSymbol *function =
            &module->function_symbols[index];
        bool local, owner;
        if (function->name == NULL || strcmp(function->name, symbol) != 0)
            continue;
        local = function->is_internal &&
                ((function->is_defined && function->is_referenced) ||
                 function->alias_target_index != SIZE_MAX);
        owner = (function->is_defined &&
                 (!function->is_internal || function->is_referenced)) ||
                function->alias_target_index != SIZE_MAX;
        facts.tu_local |= local;
        facts.nonpreemptible |= local ||
            ((!function->is_defined || owner) &&
             elf_visibility_nonpreemptible(function->visibility));
        facts.undefined_weak |= function->is_weak && !function->is_defined;
        defined |= owner;
    }
    /* A true owner (of either kind) always defeats extern-only weak status.
     * Locally defined weak symbols are handled by their definition emitter. */
    if (defined) facts.undefined_weak = false;
    return facts;
}
