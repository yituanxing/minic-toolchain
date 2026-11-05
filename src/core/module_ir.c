#include "core/module_ir.h"
#include "core/core_ir.h"
#include "core/symbol_identity.h"

#include <stdlib.h>
#include <string.h>

bool minic_core_asm_names_symbol(const char *text, size_t length,
                                 const char *symbol) {
    size_t index, name_length;
    if (text == NULL || symbol == NULL ||
        (name_length = strlen(symbol)) == 0U || name_length > length)
        return false;
    for (index = 0U; index <= length - name_length; ++index) {
        const unsigned char before = index == 0U ? 0U : (unsigned char)text[index - 1U];
        const unsigned char after = index + name_length == length ? 0U :
            (unsigned char)text[index + name_length];
        const bool left_identifier =
            (before >= 'a' && before <= 'z') || (before >= 'A' && before <= 'Z') ||
            (before >= '0' && before <= '9') || before == '_' || before == '.' ||
            before == '$';
        const bool right_identifier =
            (after >= 'a' && after <= 'z') || (after >= 'A' && after <= 'Z') ||
            (after >= '0' && after <= '9') || after == '_' || after == '.' ||
            after == '$';
        if (!left_identifier && !right_identifier &&
            memcmp(text + index, symbol, name_length) == 0)
            return true;
    }
    return false;
}

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
        free(global->references);
        for (part = 0U; global->fragments != NULL &&
                         part < global->fragment_count; ++part)
            free(global->fragments[part].relocation_symbol);
        free(global->fragments);
    }
    for (index = 0U; module->function_symbols != NULL &&
                     index < module->function_count; ++index) {
        free(module->function_symbols[index].name);
        free(module->function_symbols[index].section_name);
    }
    for (index = 0U; module->file_asms != NULL &&
                     index < module->file_asm_count; ++index)
        free(module->file_asms[index].text);
    free(module->globals);
    free(module->function_symbols);
    free(module->file_asms);
    minic_core_module_initialize(module);
}

static int module_function_name_compare(
    const MinicCoreModuleFunctionNameEntry *left,
    const MinicCoreModuleFunctionNameEntry *right) {
    int result;
    if (left->name_length < right->name_length) return -1;
    if (left->name_length > right->name_length) return 1;
    result = memcmp(left->name, right->name, left->name_length);
    if (result != 0) return result;
    if (left->function_index < right->function_index) return -1;
    if (left->function_index > right->function_index) return 1;
    return 0;
}

static int module_function_name_sort_compare(const void *left,
                                             const void *right) {
    return module_function_name_compare(left, right);
}

void minic_core_module_function_name_index_destroy(
    MinicCoreModuleFunctionNameIndex *index) {
    if (index == NULL) return;
    free(index->entries);
    index->entries = NULL;
    index->count = 0U;
}

bool minic_core_module_function_name_index_build(
    const MinicCoreModule *module, MinicCoreModuleFunctionNameIndex *index) {
    MinicCoreModuleFunctionNameEntry *entries;
    size_t i;
    if (module == NULL || index == NULL || index->entries != NULL ||
        index->count != 0U ||
        (module->function_count != 0U && module->function_symbols == NULL) ||
        module->function_count > SIZE_MAX / sizeof(*entries))
        return false;
    if (module->function_count == 0U) return true;
    entries = malloc(module->function_count * sizeof(*entries));
    if (entries == NULL) return false;
    for (i = 0U; i < module->function_count; ++i) {
        const char *name = module->function_symbols[i].name;
        if (name == NULL || name[0] == '\0') {
            free(entries);
            return false;
        }
        entries[i].name = name;
        entries[i].name_length = strlen(name);
        entries[i].function_index = i;
    }
    qsort(entries, module->function_count, sizeof(*entries),
          module_function_name_sort_compare);
    index->entries = entries;
    index->count = module->function_count;
    return true;
}

bool minic_core_module_function_name_index_find(
    const MinicCoreModuleFunctionNameIndex *index,
    const char *name, size_t name_length,
    size_t *begin, size_t *end) {
    size_t lo, hi, first;
    MinicCoreModuleFunctionNameEntry key;
    if (index == NULL || name == NULL || name_length == 0U ||
        begin == NULL || end == NULL ||
        (index->count != 0U && index->entries == NULL))
        return false;
    key.name = name;
    key.name_length = name_length;
    key.function_index = 0U;
    lo = 0U;
    hi = index->count;
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2U;
        if (module_function_name_compare(&index->entries[mid], &key) < 0)
            lo = mid + 1U;
        else hi = mid;
    }
    first = lo;
    key.function_index = SIZE_MAX;
    hi = index->count;
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2U;
        if (module_function_name_compare(&index->entries[mid], &key) <= 0)
            lo = mid + 1U;
        else hi = mid;
    }
    *begin = first;
    *end = lo;
    return true;
}

static bool valid_visibility(MinicCoreModuleVisibility visibility) {
    return visibility >= MINIC_CORE_MODULE_VISIBILITY_DEFAULT &&
           visibility <= MINIC_CORE_MODULE_VISIBILITY_PROTECTED;
}

static bool valid_power_two(size_t value) {
    return value != 0U && (value & (value - 1U)) == 0U;
}

typedef struct ModuleEmittedSymbolName {
    const char *name;
    size_t name_length;
} ModuleEmittedSymbolName;

static int module_emitted_symbol_name_compare(const void *left,
                                              const void *right) {
    const ModuleEmittedSymbolName *a = left, *b = right;
    if (a->name_length < b->name_length) return -1;
    if (a->name_length > b->name_length) return 1;
    return memcmp(a->name, b->name, a->name_length);
}

/* ELF has one assembler-symbol namespace for functions, objects and aliases.
 * Count exactly the symbols the module visitors emit: a plain declaration
 * owns no symbol, and a pruned internal definition owns no symbol. Function
 * aliases are emitted even when their source reference bit is false; object
 * aliases are emitted only when their Module global is emittable. */
static bool module_emitted_symbol_names_unique(const MinicCoreModule *module) {
    ModuleEmittedSymbolName *names;
    size_t capacity, count = 0U, i;
    bool unique = true;
    if (module->function_count > SIZE_MAX - module->global_count)
        return false;
    capacity = module->function_count + module->global_count;
    if (capacity == 0U) return true;
    if (capacity > SIZE_MAX / sizeof(*names)) return false;
    names = malloc(capacity * sizeof(*names));
    if (names == NULL) return false;
    for (i = 0U; i < module->function_count; ++i) {
        const MinicCoreModuleFunctionSymbol *symbol =
            &module->function_symbols[i];
        if (symbol->alias_target_index == SIZE_MAX &&
            (!symbol->is_defined ||
             (symbol->is_internal && !symbol->is_referenced)))
            continue;
        names[count].name = symbol->name;
        names[count++].name_length = strlen(symbol->name);
    }
    for (i = 0U; i < module->global_count; ++i) {
        const MinicCoreModuleGlobal *global = &module->globals[i];
        if (!global->emittable ||
            (global->definition_kind == MINIC_CORE_MODULE_DEFINITION_DECLARATION &&
             global->alias_target_index == SIZE_MAX))
            continue;
        names[count].name = global->name;
        names[count++].name_length = strlen(global->name);
    }
    qsort(names, count, sizeof(*names), module_emitted_symbol_name_compare);
    for (i = 1U; i < count; ++i)
        if (module_emitted_symbol_name_compare(&names[i - 1U],
                                               &names[i]) == 0) {
            unique = false;
            break;
        }
    free(names);
    return unique;
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

/* References are the pre-payload reachability edges. Once a live object has
 * been materialized, its relocation fragments must consume exactly those
 * edges in order and point at the same sealed Module symbols. Otherwise
 * reachability and emitted ELF data could disagree about a relocation's
 * target, or a target emitter could classify the same object differently.
 * Dead globals and declarations have no materialized payload to reconcile. */
static bool module_live_global_relocations_match(
    const MinicCoreModule *module, const MinicCoreModuleGlobal *global) {
    size_t fragment_index, reference_index = 0U;
    if (module == NULL || global == NULL) return false;
    for (fragment_index = 0U; fragment_index < global->fragment_count;
         ++fragment_index) {
        const MinicCoreModuleFragment *fragment =
            &global->fragments[fragment_index];
        const MinicCoreModuleReference *reference;
        const char *symbol = NULL;
        if (fragment->kind != MINIC_CORE_MODULE_FRAGMENT_RELOCATION)
            continue;
        if (reference_index >= global->reference_count ||
            global->references == NULL) return false;
        reference = &global->references[reference_index++];
        switch (reference->kind) {
        case MINIC_CORE_MODULE_REFERENCE_OBJECT:
            if (fragment->relocation_kind !=
                    MINIC_CORE_MODULE_RELOCATION_OBJECT ||
                reference->target_index >= module->global_count)
                return false;
            symbol = module->globals[reference->target_index].name;
            break;
        case MINIC_CORE_MODULE_REFERENCE_FUNCTION:
            if (fragment->relocation_kind !=
                    MINIC_CORE_MODULE_RELOCATION_FUNCTION ||
                reference->target_index >= module->function_count)
                return false;
            symbol = module->function_symbols[reference->target_index].name;
            break;
        case MINIC_CORE_MODULE_REFERENCE_OWNER_LABEL:
            /* Owner identity and lexical label spelling are checked by the
             * surrounding Module verifier and fragment validator. The
             * source label's Core BlockId is not in this reference edge. */
            if (fragment->relocation_kind !=
                    MINIC_CORE_MODULE_RELOCATION_LABEL ||
                reference->target_index != global->owner_function_index)
                return false;
            break;
        default:
            return false;
        }
        if (symbol != NULL &&
            (fragment->relocation_symbol == NULL ||
             strcmp(fragment->relocation_symbol, symbol) != 0))
            return false;
        if (symbol == NULL && reference->kind !=
                MINIC_CORE_MODULE_REFERENCE_OWNER_LABEL)
            return false;
    }
    return reference_index == global->reference_count;
}

/* A live object owns its declared typed extent plus, when explicitly admitted
 * by the producer, an integral number of trailing storage units. A Core
 * producer expresses physical backing-storage extent directly; the Module
 * verifier must not reconstruct source-language record/array rules. */
static bool module_live_global_layout_valid(
    const MinicCoreTypeContext *types, const MinicCoreType *declared,
    const MinicCoreModuleGlobal *global) {
    const MinicCoreType *base;
    if (types == NULL || declared == NULL || global == NULL ||
        !declared->has_layout || declared->alignment == 0U ||
        global->storage_alignment < declared->alignment)
        return false;
    /* Qualified CoreType views cannot silently weaken base storage
     * alignment, even when an explicit extension is present. */
    base = minic_core_type_get(types, minic_core_type_unqualified(
        types, global->core_type_id));
    if (base == NULL || !base->has_layout ||
        global->storage_alignment < base->alignment)
        return false;
    if (global->storage_extension_unit_bytes == 0U)
        return global->storage_size == declared->size;
    return global->storage_size > declared->size &&
        (global->storage_size - declared->size) %
            global->storage_extension_unit_bytes == 0U;
}

/* Check the one sealed physical data policy. ZERO_NUMERIC may contain
 * a relocation while its numeric bytes are zero; ZERO_ONLY cannot.
 * Default BSS independently forbids relocation and nonzero physical bytes. */
static bool module_live_global_definition_valid(
    const MinicCoreModuleGlobal *global) {
    const bool bss = global->default_section == MINIC_CORE_MODULE_SECTION_BSS;
    size_t index;
    if (global->data_policy == MINIC_CORE_MODULE_DATA_ZERO_ONLY &&
        global->reference_count != 0U)
        return false;
    for (index = 0U; index < global->fragment_count; ++index) {
        const MinicCoreModuleFragment *fragment = &global->fragments[index];
        if (fragment->kind == MINIC_CORE_MODULE_FRAGMENT_RELOCATION) {
            if (global->data_policy == MINIC_CORE_MODULE_DATA_ZERO_ONLY || bss)
                return false;
        } else if (fragment->kind == MINIC_CORE_MODULE_FRAGMENT_BITS &&
                   (global->data_policy != MINIC_CORE_MODULE_DATA_MATERIALIZED || bss)) {
            const uint64_t visible_bits = fragment->byte_count == 8U
                ? fragment->bits
                : fragment->bits & ((UINT64_C(1) <<
                                      (8U * fragment->byte_count)) - UINT64_C(1));
            if (visible_bits != 0U) return false;
        }
    }
    return true;
}

/* Validate one Core global without requiring unrelated symbol definitions to
 * be materialized. The complete Module verifier reuses this same predicate
 * and additionally proves function declarations and namespace uniqueness. */
static bool module_verify_global_one(const MinicCoreModule *module,
                                     size_t index) {
    const MinicCoreModuleGlobal *global = &module->globals[index];
    const MinicCoreType *type = minic_core_type_get(
        module->type_context, global->core_type_id);
    if (global->name == NULL || global->name[0] == '\0' ||
        !valid_visibility(global->visibility) ||
        (global->default_section < MINIC_CORE_MODULE_SECTION_BSS ||
         global->default_section > MINIC_CORE_MODULE_SECTION_RELRO) ||
        (global->definition_kind != MINIC_CORE_MODULE_DEFINITION_REGULAR &&
         global->definition_kind != MINIC_CORE_MODULE_DEFINITION_ZERO_FILL &&
         global->definition_kind != MINIC_CORE_MODULE_DEFINITION_DECLARATION) ||
        (global->data_policy != MINIC_CORE_MODULE_DATA_MATERIALIZED &&
         global->data_policy != MINIC_CORE_MODULE_DATA_ZERO_NUMERIC &&
         global->data_policy != MINIC_CORE_MODULE_DATA_ZERO_ONLY) ||
        (global->definition_kind == MINIC_CORE_MODULE_DEFINITION_ZERO_FILL &&
         global->data_policy != MINIC_CORE_MODULE_DATA_ZERO_ONLY) ||
        (global->definition_kind == MINIC_CORE_MODULE_DEFINITION_DECLARATION &&
         global->data_policy != MINIC_CORE_MODULE_DATA_MATERIALIZED) ||
        (global->definition_kind == MINIC_CORE_MODULE_DEFINITION_DECLARATION &&
         (global->storage_size != 0U || global->storage_extension_unit_bytes != 0U ||
          global->storage_alignment != 0U ||
          global->reference_count != 0U || global->fragment_count != 0U)) ||
        (global->definition_kind == MINIC_CORE_MODULE_DEFINITION_ZERO_FILL &&
         global->storage_extension_unit_bytes != 0U) ||
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
                reference->kind < MINIC_CORE_MODULE_REFERENCE_OBJECT ||
                reference->kind > MINIC_CORE_MODULE_REFERENCE_OWNER_LABEL)
                return false;
            if (reference->kind == MINIC_CORE_MODULE_REFERENCE_OWNER_LABEL) {
                const MinicCoreModuleFunctionSymbol *owner =
                    &module->function_symbols[reference->target_index];
                if (global->owner_function_index != reference->target_index ||
                    !owner->is_defined || owner->specialization_source_index != SIZE_MAX)
                    return false;
            }
        }
    }
    if (global->alias_target_index != SIZE_MAX) {
        const MinicCoreModuleGlobal *target;
        if (global->definition_kind != MINIC_CORE_MODULE_DEFINITION_DECLARATION ||
            global->alias_target_index >= module->global_count ||
            global->alias_target_index == index)
            return false;
        target = &module->globals[global->alias_target_index];
        if ((global->emittable && !target->emittable) ||
            target->definition_kind != MINIC_CORE_MODULE_DEFINITION_REGULAR ||
            target->alias_target_index != SIZE_MAX ||
            target->name == NULL || target->name[0] == '\0')
            return false;
    }
    if (!global->emittable)
        return global->fragment_count == 0U;
    if (type == NULL) return false;
    if (global->definition_kind == MINIC_CORE_MODULE_DEFINITION_DECLARATION)
        return global->fragment_count == 0U;
    if (!type->has_layout || !valid_power_two(global->storage_alignment) ||
        !minic_core_module_global_fragments_valid(global) ||
        !module_live_global_relocations_match(module, global) ||
        !module_live_global_layout_valid(module->type_context, type, global) ||
        !module_live_global_definition_valid(global)) return false;
    return true;
}

bool minic_core_module_verify_global(const MinicCoreModule *module,
                                     size_t ordinal) {
    return module != NULL && module->finalized &&
        module->type_context != NULL && ordinal < module->global_count &&
        module->globals != NULL &&
        (module->function_count == 0U || module->function_symbols != NULL) &&
        module_verify_global_one(module, ordinal);
}

bool minic_core_module_verify(const MinicCoreModule *module) {
    size_t index;
    if (module == NULL || !module->finalized || module->type_context == NULL ||
        (module->global_count != 0U && module->globals == NULL) ||
        (module->function_count != 0U && module->function_symbols == NULL) ||
        (module->file_asm_count != 0U && module->file_asms == NULL))
        return false;
    for (index = 0U; index < module->global_count; ++index)
        if (!module_verify_global_one(module, index)) return false;
    for (index = 0U; index < module->function_count; ++index) {
        const MinicCoreModuleFunctionSymbol *symbol = &module->function_symbols[index];
        if (symbol->name == NULL || symbol->name[0] == '\0' ||
            !valid_visibility(symbol->visibility) ||
            (symbol->specialization_source_index != SIZE_MAX &&
             (symbol->specialization_source_index >= module->function_count ||
              symbol->specialization_source_index == index))) return false;
        if (symbol->specialization_source_index != SIZE_MAX) {
            const MinicCoreModuleFunctionSymbol *source =
                &module->function_symbols[symbol->specialization_source_index];
            if (!symbol->is_defined || !symbol->is_internal ||
                symbol->alias_target_index != SIZE_MAX ||
                !source->is_defined ||
                source->specialization_source_index != SIZE_MAX) return false;
        }
        if (symbol->alias_target_index != SIZE_MAX) {
            const MinicCoreModuleFunctionSymbol *target;
            if (symbol->is_defined || symbol->alias_target_index >= module->function_count ||
                symbol->alias_target_index == index)
                return false;
            target = &module->function_symbols[symbol->alias_target_index];
            if (!target->is_defined || target->alias_target_index != SIZE_MAX ||
                target->name == NULL || target->name[0] == '\0')
                return false;
        }
    }
    if (!module_emitted_symbol_names_unique(module)) return false;
    for (index = 0U; index < module->file_asm_count; ++index)
        if ((module->file_asms[index].length != 0U &&
             module->file_asms[index].text == NULL) ||
            (module->file_asms[index].text != NULL &&
             memchr(module->file_asms[index].text, '\0',
                    module->file_asms[index].length) != NULL))
            return false;
    return true;
}

typedef struct ModuleExportedLocalLabel {
    const char *name;
    size_t owner_function_index;
} ModuleExportedLocalLabel;

static int module_exported_label_compare(const void *left, const void *right) {
    const ModuleExportedLocalLabel *a = left, *b = right;
    return strcmp(a->name, b->name);
}

static bool module_label_has_canonical_owner(
    const ModuleExportedLocalLabel *labels, size_t label_count,
    const char *name, size_t owner) {
    size_t begin = 0U, end = label_count;
    while (begin < end) {
        const size_t middle = begin + (end - begin) / 2U;
        const int comparison = strcmp(name, labels[middle].name);
        if (comparison < 0) end = middle;
        else if (comparison > 0) begin = middle + 1U;
        else return labels[middle].owner_function_index == owner;
    }
    return false;
}

bool minic_core_module_verify_label_bindings(
    const MinicCoreModule *module, const MinicCoreFunction *functions,
    size_t function_count) {
    ModuleExportedLocalLabel *labels = NULL;
    size_t label_count = 0U, capacity = 0U, function_index;
    bool valid = false;
    if (!minic_core_module_verify(module) ||
        module->function_count != function_count ||
        (function_count != 0U && functions == NULL))
        return false;
    /* Only Core definitions selected for machine emission export aliases.
     * Function clones execute the original body but must not export its
     * canonical local labels a second time. */
    for (function_index = 0U; function_index < function_count; ++function_index) {
        const MinicCoreModuleFunctionSymbol *symbol =
            &module->function_symbols[function_index];
        const MinicCoreFunction *function = &functions[function_index];
        size_t block_index;
        if (!symbol->is_defined || (symbol->is_internal && !symbol->is_referenced))
            continue;
        if (function->block_count != 0U && function->blocks == NULL)
            goto done;
        for (block_index = 0U; block_index < function->block_count; ++block_index) {
            const char *name = function->blocks[block_index].local_label_symbol;
            if (name == NULL) continue;
            if (symbol->specialization_source_index != SIZE_MAX ||
                !minic_core_label_symbol_valid(name) ||
                capacity == SIZE_MAX / sizeof(*labels)) goto done;
            if (label_count == capacity) {
                const size_t next_capacity = capacity == 0U ? 16U :
                    capacity > SIZE_MAX / (2U * sizeof(*labels))
                        ? SIZE_MAX / sizeof(*labels) : capacity * 2U;
                ModuleExportedLocalLabel *grown;
                if (next_capacity <= capacity) goto done;
                grown = realloc(labels, next_capacity * sizeof(*labels));
                if (grown == NULL) goto done;
                labels = grown;
                capacity = next_capacity;
            }
            labels[label_count].name = name;
            labels[label_count++].owner_function_index = function_index;
        }
    }
    if (label_count > 1U)
        qsort(labels, label_count, sizeof(*labels), module_exported_label_compare);
    for (function_index = 1U; function_index < label_count; ++function_index)
        if (strcmp(labels[function_index - 1U].name, labels[function_index].name) == 0)
            goto done; /* Duplicate assembler symbols across function bodies. */
    for (function_index = 0U; function_index < module->global_count; ++function_index) {
        const MinicCoreModuleGlobal *global = &module->globals[function_index];
        size_t fragment_index, reference_index = 0U;
        if (!global->emittable ||
            global->definition_kind == MINIC_CORE_MODULE_DEFINITION_DECLARATION)
            continue;
        for (fragment_index = 0U; fragment_index < global->fragment_count;
             ++fragment_index) {
            const MinicCoreModuleFragment *fragment = &global->fragments[fragment_index];
            const MinicCoreModuleReference *reference;
            if (fragment->kind != MINIC_CORE_MODULE_FRAGMENT_RELOCATION) continue;
            if (reference_index >= global->reference_count) goto done;
            reference = &global->references[reference_index++];
            if (reference->kind != MINIC_CORE_MODULE_REFERENCE_OWNER_LABEL)
                continue;
            if (fragment->relocation_symbol == NULL ||
                !module_label_has_canonical_owner(
                    labels, label_count, fragment->relocation_symbol,
                    reference->target_index)) goto done;
        }
    }
    valid = true;
done:
    free(labels);
    return valid;
}
