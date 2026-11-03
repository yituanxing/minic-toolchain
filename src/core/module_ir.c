#include "core/module_ir.h"
#include "core/symbol_identity.h"

#include <stdlib.h>
#include <string.h>

void minic_core_module_initialize(MinicCoreModule *module) {
    if (module != NULL) (void)memset(module, 0, sizeof(*module));
}

void minic_core_module_destroy(MinicCoreModule *module) {
    size_t index, part;
    if (module == NULL) return;
    for (index = 0U; module->globals != NULL && index < module->global_count; ++index) {
        MinicCoreModuleGlobal *global = &module->globals[index];
        free(global->name);
        free(global->section_name);
        free(global->alias_target_name);
        free(global->references);
        for (part = 0U; global->fragments != NULL &&
                         part < global->fragment_count; ++part)
            free(global->fragments[part].relocation_symbol);
        free(global->fragments);
    }
    for (index = 0U; module->function_symbols != NULL &&
                     index < module->function_count; ++index) {
        free(module->function_symbols[index].name);
        free(module->function_symbols[index].source_name);
        free(module->function_symbols[index].section_name);
        free(module->function_symbols[index].alias_target_name);
    }
    for (index = 0U; module->file_asms != NULL &&
                     index < module->file_asm_count; ++index)
        free(module->file_asms[index].text);
    free(module->globals);
    free(module->function_symbols);
    free(module->file_asms);
    minic_core_module_initialize(module);
}

static bool valid_visibility(MinicCoreModuleVisibility visibility) {
    return visibility >= MINIC_CORE_MODULE_VISIBILITY_DEFAULT &&
           visibility <= MINIC_CORE_MODULE_VISIBILITY_PROTECTED;
}

static bool valid_power_two(size_t value) {
    return value != 0U && (value & (value - 1U)) == 0U;
}

bool minic_core_module_global_fragments_valid(const MinicCoreModuleGlobal *global) {
    size_t index, offset = 0U;
    if (global == NULL ||
        (global->fragment_count != 0U && global->fragments == NULL))
        return false;
    for (index = 0U; index < global->fragment_count; ++index) {
        const MinicCoreModuleFragment *fragment = &global->fragments[index];
        /* Check the cursor first: subtraction must never wrap on malformed
         * external Module IR, even if an offset happens to be forged. */
        if (offset > global->storage_size ||
            fragment->byte_offset != offset || fragment->byte_count == 0U ||
            fragment->byte_count > global->storage_size - offset)
            return false;
        if (fragment->kind == MINIC_CORE_MODULE_FRAGMENT_BITS) {
            if (fragment->byte_count != 1U && fragment->byte_count != 2U &&
                fragment->byte_count != 4U && fragment->byte_count != 8U)
                return false;
            if (fragment->relocation_symbol != NULL) return false;
        } else if (fragment->kind == MINIC_CORE_MODULE_FRAGMENT_RELOCATION) {
            if (fragment->byte_count != 1U && fragment->byte_count != 2U &&
                fragment->byte_count != 4U && fragment->byte_count != 8U)
                return false;
            if (fragment->relocation_kind == MINIC_CORE_MODULE_RELOCATION_LABEL) {
                if (!minic_core_label_symbol_valid(fragment->relocation_symbol) ||
                    fragment->relocation_addend != 0) return false;
            } else if (fragment->relocation_kind == MINIC_CORE_MODULE_RELOCATION_OBJECT ||
                       fragment->relocation_kind == MINIC_CORE_MODULE_RELOCATION_FUNCTION) {
                if (!minic_core_assembler_symbol_lexically_safe(
                        fragment->relocation_symbol)) return false;
            } else return false;
        } else if (fragment->kind != MINIC_CORE_MODULE_FRAGMENT_ZERO)
            return false;
        else if (fragment->relocation_symbol != NULL) return false;
        offset += fragment->byte_count;
    }
    return offset == global->storage_size;
}

bool minic_core_module_verify(const MinicCoreModule *module) {
    size_t index;
    if (module == NULL || !module->finalized || module->type_context == NULL ||
        (module->global_count != 0U && module->globals == NULL) ||
        (module->function_count != 0U && module->function_symbols == NULL) ||
        (module->file_asm_count != 0U && module->file_asms == NULL))
        return false;
    for (index = 0U; index < module->global_count; ++index) {
        const MinicCoreModuleGlobal *global = &module->globals[index];
        const MinicCoreType *type = minic_core_type_get(
            module->type_context, global->core_type_id);
        if (global->name == NULL || global->name[0] == '\0' ||
            !valid_visibility(global->visibility) ||
            (global->reference_count != 0U && global->references == NULL) ||
            (global->owner_function_index != SIZE_MAX &&
             global->owner_function_index >= module->function_count))
            return false;
        {
            size_t reference_index;
            for (reference_index = 0U; reference_index < global->reference_count;
                 ++reference_index) {
                const MinicCoreModuleReference *reference =
                    &global->references[reference_index];
                if ((reference->kind == MINIC_CORE_MODULE_REFERENCE_OBJECT &&
                     reference->target_index >= module->global_count) ||
                    (reference->kind == MINIC_CORE_MODULE_REFERENCE_FUNCTION &&
                     reference->target_index >= module->function_count) ||
                    (reference->kind == MINIC_CORE_MODULE_REFERENCE_OWNER_LABEL &&
                     reference->target_index >= module->function_count) ||
                    reference->kind > MINIC_CORE_MODULE_REFERENCE_OWNER_LABEL)
                    return false;
                if (reference->kind == MINIC_CORE_MODULE_REFERENCE_OWNER_LABEL) {
                    const MinicCoreModuleFunctionSymbol *owner =
                        &module->function_symbols[reference->target_index];
                    if (global->owner_function_index != reference->target_index ||
                        !owner->is_defined || owner->is_specialization)
                        return false;
                }
            }
        }
        if (global->is_alias) {
            const MinicCoreModuleGlobal *target;
            if (!global->is_extern || global->alias_target_index >= module->global_count ||
                global->alias_target_index == index || global->alias_target_name == NULL)
                return false;
            target = &module->globals[global->alias_target_index];
            if ((global->emittable && !target->emittable) ||
                target->is_extern || target->is_tentative || target->is_alias ||
                target->name == NULL ||
                strcmp(target->name, global->alias_target_name) != 0)
                return false;
        } else if (global->alias_target_index != SIZE_MAX ||
                   global->alias_target_name != NULL) return false;
        if (!global->emittable) {
            if (global->fragment_count != 0U) return false;
            continue;
        }
        if (type == NULL) return false;
        if (global->is_extern) {
            if (global->fragment_count != 0U) return false;
            continue;
        }
        if (global->default_section > MINIC_CORE_MODULE_SECTION_RELRO ||
            global->payload_kind > MINIC_CORE_MODULE_PAYLOAD_SCALAR_BITS)
            return false;
        if (!type->has_layout || !valid_power_two(global->storage_alignment) ||
            !minic_core_module_global_fragments_valid(global)) return false;
    }
    for (index = 0U; index < module->function_count; ++index) {
        const MinicCoreModuleFunctionSymbol *symbol = &module->function_symbols[index];
        if (symbol->name == NULL || symbol->name[0] == '\0' ||
            !valid_visibility(symbol->visibility) ||
            (symbol->is_specialization &&
             (symbol->specialization_source_index >= module->function_count ||
              symbol->specialization_source_index == index))) return false;
        if (symbol->is_specialization) {
            const MinicCoreModuleFunctionSymbol *source =
                &module->function_symbols[symbol->specialization_source_index];
            if (!source->is_defined || source->is_specialization) return false;
        }
        if (symbol->alias_target_index != SIZE_MAX) {
            const MinicCoreModuleFunctionSymbol *target;
            if (symbol->is_defined || symbol->alias_target_index >= module->function_count ||
                symbol->alias_target_index == index || symbol->alias_target_name == NULL)
                return false;
            target = &module->function_symbols[symbol->alias_target_index];
            if (!target->is_defined || target->alias_target_index != SIZE_MAX ||
                target->name == NULL ||
                strcmp(target->name, symbol->alias_target_name) != 0)
                return false;
        } else if (symbol->alias_target_name != NULL) return false;
    }
    for (index = 0U; index < module->file_asm_count; ++index)
        if ((module->file_asms[index].length != 0U &&
             module->file_asms[index].text == NULL) ||
            (module->file_asms[index].text != NULL &&
             memchr(module->file_asms[index].text, '\0',
                    module->file_asms[index].length) != NULL))
            return false;
    return true;
}
