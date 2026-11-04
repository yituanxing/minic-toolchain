#include "target/common/program_emit.h"
#include "target/common/elf_alias_emit.h"
#include "core/symbol_identity.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* POSIX functions may be hidden by strict C11 header feature macros.
 * Repeated compatible declarations work in GNU/POSIX host toolchains. */
extern int mkstemp(char *);
extern FILE *fdopen(int, const char *);

/* File-scope GNU ASM is sealed into ModuleIR at source ingress; the
 * machine writer emits the original bytes without reopening C Program. */
bool minic_program_emit_source_file_asm_module(
    FILE *out, const MinicCoreModule *module) {
    size_t index;
    if (out == NULL || module == NULL || !module->finalized ||
        (module->file_asm_count != 0U && module->file_asms == NULL))
        return false;
    if (module->file_asm_count == 0U) return true;
    if (fputs(".text\n", out) == EOF) return false;
    for (index = 0U; index < module->file_asm_count; ++index) {
        const MinicCoreModuleFileAsm *source = &module->file_asms[index];
        if (source->text == NULL ||
            memchr(source->text, '\0', source->length) != NULL ||
            (source->length != 0U &&
             fwrite(source->text, 1U, source->length, out) != source->length) ||
            fputc('\n', out) == EOF)
            return false;
    }
    return fputs(".text\n", out) != EOF;
}

typedef struct MinicProgramAsmUse {
    const char *text;
    size_t length;
} MinicProgramAsmUse;

typedef struct MinicProgramSymbolUses {
    const char **names; /* borrowed immutable Core/source symbol names */
    size_t count;
    size_t capacity;
    MinicProgramAsmUse *asm_uses;
    size_t asm_count;
    size_t asm_capacity;
} MinicProgramSymbolUses;

static bool minic_program_symbol_use_add(MinicProgramSymbolUses *uses,
                                                 const char *name) {
    const char **next;
    size_t capacity;
    if (name == NULL || name[0] == '\0') return true;
    if (uses->count == uses->capacity) {
        capacity = uses->capacity == 0U ? 32U : uses->capacity * 2U;
        if (capacity < uses->capacity || capacity > SIZE_MAX / sizeof(*next))
            return false;
        next = realloc(uses->names, capacity * sizeof(*next));
        if (next == NULL) return false;
        uses->names = next;
        uses->capacity = capacity;
    }
    uses->names[uses->count++] = name;
    return true;
}

static int minic_program_symbol_name_compare(const void *left,
                                                     const void *right) {
    const char *const a = *(const char *const *)left;
    const char *const b = *(const char *const *)right;
    return strcmp(a, b);
}

static bool minic_program_symbol_use_has(const MinicProgramSymbolUses *uses,
                                                 const char *name) {
    return name != NULL && uses->count != 0U &&
           bsearch(&name, uses->names, uses->count, sizeof(*uses->names),
                   minic_program_symbol_name_compare) != NULL;
}

static bool minic_program_symbol_use_add_asm(MinicProgramSymbolUses *uses,
                                                    const MinicCoreInlineAsm *source) {
    MinicProgramAsmUse *next;
    size_t capacity;
    if (source == NULL || source->template_text == NULL) return false;
    if (uses->asm_count == uses->asm_capacity) {
        capacity = uses->asm_capacity == 0U ? 8U : uses->asm_capacity * 2U;
        if (capacity < uses->asm_capacity || capacity > SIZE_MAX / sizeof(*next))
            return false;
        next = realloc(uses->asm_uses, capacity * sizeof(*next));
        if (next == NULL) return false;
        uses->asm_uses = next;
        uses->asm_capacity = capacity;
    }
    uses->asm_uses[uses->asm_count].text = source->template_text;
    uses->asm_uses[uses->asm_count++].length = source->template_length;
    return true;
}

/* Source-owned file/inline ASM may name an external symbol without a Core
 * address or CALL. Check complete assembler tokens, not arbitrary substrings. */
static bool minic_program_asm_names_symbol(const char *source,
                                                   size_t length,
                                                   const char *name) {
    size_t index, name_length;
    if (source == NULL || name == NULL || (name_length = strlen(name)) == 0U ||
        name_length > length)
        return false;
    for (index = 0U; index <= length - name_length; ++index) {
        const unsigned char before = index == 0U ? 0U : (unsigned char)source[index - 1U];
        const unsigned char after = index + name_length == length ? 0U :
            (unsigned char)source[index + name_length];
        const bool left_identifier =
            (before >= 'a' && before <= 'z') || (before >= 'A' && before <= 'Z') ||
            (before >= '0' && before <= '9') || before == '_' || before == '.' ||
            before == '$';
        const bool right_identifier =
            (after >= 'a' && after <= 'z') || (after >= 'A' && after <= 'Z') ||
            (after >= '0' && after <= '9') || after == '_' || after == '.' ||
            after == '$';
        if (!left_identifier && !right_identifier &&
            memcmp(source + index, name, name_length) == 0)
            return true;
    }
    return false;
}

static bool minic_program_symbol_used_in_source_asm_module(
    const MinicProgramSymbolUses *uses, const char *name,
    const MinicCoreModule *module) {
    size_t index;
    if (uses == NULL || module == NULL) return false;
    for (index = 0U; index < module->file_asm_count; ++index)
        if (minic_program_asm_names_symbol(module->file_asms[index].text,
                                           module->file_asms[index].length, name))
            return true;
    /* Only Core ASM instructions in blocks the target actually emits count.
     * A dead static inline body with a hidden declaration is not a use. */
    for (index = 0U; index < uses->asm_count; ++index)
        if (minic_program_asm_names_symbol(
                uses->asm_uses[index].text, uses->asm_uses[index].length, name))
            return true;
    return false;
}

/* Collect symbol use from *emitted* Core, plus live global initializers.
 * Declaration-only visibility is not an ELF reference: GNU AS creates an
 * undefined hidden symbol for a bare `.hidden foo` even with no relocation,
 * making an otherwise linkable Linux vDSO/EFI object fail GNU ld. */
static bool minic_program_emit_collect_symbol_uses_module(
    MinicProgramSymbolUses *uses,
    const MinicCoreFunction *functions, size_t function_count,
    const MinicProgramEmitConfig *config, const MinicCoreModule *module) {
    size_t index, ordinal, block_id;
    if (uses == NULL || config == NULL ||
        module == NULL ||
        (function_count != 0U && functions == NULL) ||
        !module->finalized || module->function_count != function_count ||
        (module->global_count != 0U && module->globals == NULL) ||
        (function_count != 0U && module->function_symbols == NULL) ||
        (module->file_asm_count != 0U && module->file_asms == NULL))
        return false;
    {
        for (index = 0U; index < module->global_count; ++index) {
            const MinicCoreModuleGlobal *object = &module->globals[index];
            if (!object->emittable) continue;
            if (object->alias_target_index != SIZE_MAX) {
                const char *alias_target;
                if (object->alias_target_index >= module->global_count) return false;
                alias_target = module->globals[object->alias_target_index].name;
                if (alias_target == NULL || alias_target[0] == '\0' ||
                    !minic_program_symbol_use_add(uses, alias_target)) return false;
            }
            if (object->fragment_count != 0U && object->fragments == NULL)
                return false;
            for (ordinal = 0U; ordinal < object->fragment_count; ++ordinal) {
                const MinicCoreModuleFragment *fragment = &object->fragments[ordinal];
                if (fragment->kind == MINIC_CORE_MODULE_FRAGMENT_RELOCATION &&
                    fragment->relocation_kind == MINIC_CORE_MODULE_RELOCATION_LABEL &&
                    !minic_core_label_symbol_valid(
                        fragment->relocation_symbol))
                    return false;
                if (fragment->kind == MINIC_CORE_MODULE_FRAGMENT_RELOCATION &&
                    fragment->relocation_kind != MINIC_CORE_MODULE_RELOCATION_LABEL &&
                    (fragment->relocation_symbol == NULL ||
                     fragment->relocation_symbol[0] == '\0' ||
                     !minic_program_symbol_use_add(
                         uses, fragment->relocation_symbol)))
                    return false;
            }
        }
        for (index = 0U; index < module->function_count; ++index) {
            const MinicCoreModuleFunctionSymbol *function =
                &module->function_symbols[index];
            if (function->alias_target_index != SIZE_MAX) {
                const char *alias_target;
                if (function->alias_target_index >= module->function_count) return false;
                alias_target = module->function_symbols[function->alias_target_index].name;
                if (alias_target == NULL || alias_target[0] == '\0' ||
                    !minic_program_symbol_use_add(uses, alias_target)) return false;
            }
        }
    }
    for (index = 0U; index < function_count; ++index) {
        const MinicCoreFunction *function = &functions[index];
        bool *reachable = NULL;
        if (!(minic_program_module_function_selected(
                &module->function_symbols[index], config->selection) &&
              module->function_symbols[index].is_defined))
            continue;
        reachable = calloc(function->block_count == 0U ? 1U : function->block_count,
                           sizeof(*reachable));
        if (reachable == NULL || !minic_core_function_mark_reachable_blocks(
                function, reachable, function->block_count)) {
            free(reachable);
            return false;
        }
        for (block_id = 0U; block_id < function->block_count; ++block_id) {
            const MinicCoreBlock *block = &function->blocks[block_id];
            if (!reachable[block_id]) continue;
            for (ordinal = 0U; ordinal < block->instruction_count; ++ordinal) {
                const MinicCoreInstructionId id = block->instructions[ordinal];
                const MinicCoreInstruction *ins;
                const char *name = NULL;
                if (id >= function->instruction_count) { free(reachable); return false; }
                ins = &function->instructions[id];
                if (ins->kind == MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS) {
                    if (ins->value.global_id >= function->global_count) {
                        free(reachable); return false;
                    }
                    name = function->globals[ins->value.global_id].name;
                } else if (ins->kind == MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS) {
                    if (ins->value.function_symbol_id >= function->function_symbol_count) {
                        free(reachable); return false;
                    }
                    name = function->function_symbols[ins->value.function_symbol_id].name;
                } else if (ins->kind == MINIC_CORE_INSTRUCTION_CALL) {
                    if (ins->value.call.callee_id >= function->callee_count) {
                        free(reachable); return false;
                    }
                    name = function->callees[ins->value.call.callee_id].name;
                }
                if (ins->kind == MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM ||
                    ins->kind == MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM ||
                    ins->kind == MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INPUT_INLINE_ASM ||
                    ins->kind == MINIC_CORE_INSTRUCTION_MEMORY_READWRITE_SCALAR_INPUT_INLINE_ASM ||
                    ins->kind == MINIC_CORE_INSTRUCTION_SCALAR_INPUT_INLINE_ASM ||
                    ins->kind == MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM) {
                    MinicCoreInlineAsmId asm_id = ins->value.inline_asm_id;
                    if (ins->kind == MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INPUT_INLINE_ASM)
                        asm_id = ins->value.register_output_input_inline_asm.inline_asm_id;
                    if (ins->kind == MINIC_CORE_INSTRUCTION_MEMORY_READWRITE_SCALAR_INPUT_INLINE_ASM)
                        asm_id = ins->value.memory_readwrite_scalar_input_inline_asm.inline_asm_id;
                    if (ins->kind == MINIC_CORE_INSTRUCTION_SCALAR_INPUT_INLINE_ASM)
                        asm_id = ins->value.scalar_input_inline_asm.inline_asm_id;
                    if (ins->kind == MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM)
                        asm_id = ins->value.structured_inline_asm.inline_asm_id;
                    if (asm_id >= function->inline_asm_count ||
                        !minic_program_symbol_use_add_asm(
                            uses, &function->inline_asms[asm_id])) {
                        free(reachable); return false;
                    }
                }
                if (!minic_program_symbol_use_add(uses, name)) {
                    free(reachable); return false;
                }
            }
        }
        free(reachable);
    }
    if (uses->count != 0U)
        qsort(uses->names, uses->count, sizeof(*uses->names),
              minic_program_symbol_name_compare);
    return true;
}

/* Undefined ELF bindings belong to real references, not mere C declarations.
 * In particular, a referenced GNU extern weak symbol must retain `.weak`:
 * the ELF linker may legally resolve it to zero without a definition.  A
 * default-strong undefined reference silently changes that source contract.
 * Definitions and aliases already emit their own binding directives. */
static bool minic_program_emit_undefined_symbol_visibility_module(
    FILE *out, const MinicCoreFunction *functions, size_t function_count,
    const MinicProgramEmitConfig *config, const MinicCoreModule *module) {
    size_t index;
    MinicProgramSymbolUses uses = {0};
    bool ok = true;
    if (out == NULL || module == NULL) return false;
    if (!minic_program_emit_collect_symbol_uses_module(
            &uses, functions, function_count, config, module)) {
        free(uses.names);
        free(uses.asm_uses);
        return false;
    }
    {
        for (index = 0U; index < module->global_count; ++index) {
            const MinicCoreModuleGlobal *object = &module->globals[index];
            if (object->is_extern && !object->is_internal &&
                object->alias_target_index == SIZE_MAX) {
                const char *directive =
                    minic_multi_elf_module_visibility_directive(object->visibility);
                const bool used =
                    minic_program_symbol_use_has(&uses, object->name) ||
                    minic_program_symbol_used_in_source_asm_module(
                        &uses, object->name, module);
                if (used && (object->name == NULL ||
                    (object->is_weak &&
                     fprintf(out, ".weak %s\n", object->name) < 0) ||
                    (object->visibility != MINIC_CORE_MODULE_VISIBILITY_DEFAULT &&
                     (directive == NULL ||
                      fprintf(out, "%s %s\n", directive, object->name) < 0)))) {
                    ok = false;
                    break;
                }
            }
        }
        for (index = 0U; ok && index < module->function_count; ++index) {
            const MinicCoreModuleFunctionSymbol *function =
                &module->function_symbols[index];
            if (!function->is_defined && !function->is_internal &&
                function->alias_target_index == SIZE_MAX) {
                const char *directive =
                    minic_multi_elf_module_visibility_directive(function->visibility);
                const bool used =
                    minic_program_symbol_use_has(&uses, function->name) ||
                    minic_program_symbol_used_in_source_asm_module(
                        &uses, function->name, module);
                if (used && (function->name == NULL ||
                    (function->is_weak &&
                     fprintf(out, ".weak %s\n", function->name) < 0) ||
                    (function->visibility != MINIC_CORE_MODULE_VISIBILITY_DEFAULT &&
                     (directive == NULL ||
                      fprintf(out, "%s %s\n", directive, function->name) < 0)))) {
                    ok = false;
                    break;
                }
            }
        }
    }
    free(uses.names);
    free(uses.asm_uses);
    return ok;
}

bool minic_program_emit_write_module(
    const char *path, const MinicCoreModule *module,
    const MinicTargetInfo *target, const MinicCoreFunction *functions,
    size_t function_count, bool function_sections,
    const MinicProgramEmitConfig *config, void *context,
    MinicProgramVisitModuleGlobal visit_module_global) {
    FILE *out;
    char *temporary;
    size_t path_length;
    int descriptor;
    bool ok;
    bool functions_ok;
    if (path == NULL || target == NULL || config == NULL ||
        module == NULL || !module->finalized || visit_module_global == NULL ||
        module->function_count != function_count ||
        config->emit_function == NULL ||
        (function_count != 0U && functions == NULL))
        return false;
    /* Stage next to the destination, without touching the last good .s.
     * Both output and ISA finalizers operate on this private file. Only a
     * successful close/finalization may atomically replace the public path. */
    path_length = strlen(path);
    if (path_length > SIZE_MAX - sizeof(".minic-XXXXXX")) return false;
    temporary = malloc(path_length + sizeof(".minic-XXXXXX"));
    if (temporary == NULL) return false;
    memcpy(temporary, path, path_length);
    memcpy(temporary + path_length, ".minic-XXXXXX",
           sizeof(".minic-XXXXXX"));
    descriptor = mkstemp(temporary);
    if (descriptor < 0) { free(temporary); return false; }
    out = fdopen(descriptor, "wb");
    if (out == NULL) {
        (void)close(descriptor);
        (void)remove(temporary);
        free(temporary);
        return false;
    }
    ok = config->prefix == NULL ||
         config->prefix(out, module, target, functions, function_count,
                        function_sections, context);
    if (ok) ok = minic_program_emit_undefined_symbol_visibility_module(
        out, functions, function_count, config, module);
    if (ok)
        ok = minic_program_visit_module_globals(
            out, module, target, visit_module_global, context);
    if (ok && config->between != NULL)
        ok = config->between(out, module, target, functions, function_count,
                             function_sections, context);
    if (ok || config->continue_after_failure) {
        functions_ok = minic_program_visit_prepared_functions_module(
            out, module, target, functions, function_count,
            function_sections, config->selection, config->continue_after_failure,
            config->emit_function, context);
        ok = ok && functions_ok;
    }
    if (ok && config->trailer != NULL && fputs(config->trailer, out) == EOF)
        ok = false;
    /* Preserve the write error even if close also fails. Never finalize
     * or leave a partial output after an unsuccessful machine emission. */
    if (fclose(out) != 0) ok = false;
    if (ok && config->finalize != NULL)
        ok = config->finalize(temporary, target, context);
    if (ok && rename(temporary, path) != 0)
        ok = false;
    if (!ok) (void)remove(temporary);
    free(temporary);
    return ok;
}
