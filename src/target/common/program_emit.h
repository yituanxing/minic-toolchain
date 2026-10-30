#ifndef MINIC_TARGET_COMMON_PROGRAM_EMIT_H
#define MINIC_TARGET_COMMON_PROGRAM_EMIT_H

#include "target/common/program_visit.h"
#include "target/common/elf_alias_emit.h"
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

/* One output transaction and one global/Core walk for ALL five targets.
 * Core declarations/definitions and cleanup are common; ABI, source-ASM
 * provenance, section syntax and machine instructions remain target-owned.
 * In particular, no LP64-only calling convention or frame offsets enter
 * this interface. Every callback sees the original target and Core mapping. */
typedef bool (*MinicProgramEmitStage)(
    FILE *, const MinicC0Program *, const MinicTargetInfo *,
    const MinicCoreFunction *, size_t, bool, void *);
typedef bool (*MinicProgramEmitFinalize)(
    const char *, const MinicTargetInfo *, void *);

typedef struct MinicProgramEmitConfig {
    MinicProgramVisitGlobal emit_global;
    MinicProgramVisitPreparedFunction emit_function;
    MinicProgramEmitStage prefix; /* before globals: e.g. RV32 .option norvc */
    MinicProgramEmitStage between; /* file-scope asm / declaration bindings */
    const char *trailer;          /* optional target GNU-stack note */
    MinicProgramEmitFinalize finalize; /* strict ISA audit or legalization */
    MinicProgramFunctionSelection selection;
    bool continue_after_failure; /* opt-in census; never retain partial .s */
} MinicProgramEmitConfig;

/* GNU basic file-scope asm is source-owned text, not generated machine
 * instruction selection. Emit the original bytes once between globals and
 * functions for targets with a GNU assembler. Never treat embedded NUL as
 * a truncation boundary, and restore .text after arbitrary source sections. */
static inline bool minic_program_emit_source_file_asm(
    FILE *out, const MinicC0Program *program) {
    size_t i;
    if (out == NULL || program == NULL) return false;
    if (program->file_asm_count == 0U) return true;
    if (fputs(".text\n", out) == EOF) return false;
    for (i = 0U; i < program->file_asm_count; ++i) {
        const MinicFileAsm *source = &program->file_asms[i];
        if (source->text == NULL ||
            memchr(source->text, '\0', source->length) != NULL ||
            (source->length != 0U &&
             fwrite(source->text, 1U, source->length, out) != source->length) ||
            fputc('\n', out) == EOF)
            return false;
    }
    return fputs(".text\n", out) != EOF;
}

static inline bool minic_program_emit_source_file_asm_stage(
    FILE *out, const MinicC0Program *program,
    const MinicTargetInfo *target, const MinicCoreFunction *functions,
    size_t function_count, bool function_sections, void *context) {
    (void)target;
    (void)functions;
    (void)function_count;
    (void)function_sections;
    (void)context;
    return minic_program_emit_source_file_asm(out, program);
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

static inline bool minic_program_symbol_use_add(MinicProgramSymbolUses *uses,
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

static inline int minic_program_symbol_name_compare(const void *left,
                                                     const void *right) {
    const char *const a = *(const char *const *)left;
    const char *const b = *(const char *const *)right;
    return strcmp(a, b);
}

static inline bool minic_program_symbol_use_has(const MinicProgramSymbolUses *uses,
                                                 const char *name) {
    return name != NULL && uses->count != 0U &&
           bsearch(&name, uses->names, uses->count, sizeof(*uses->names),
                   minic_program_symbol_name_compare) != NULL;
}

static inline bool minic_program_symbol_use_add_asm(MinicProgramSymbolUses *uses,
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
static inline bool minic_program_asm_names_symbol(const char *source,
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

static inline bool minic_program_symbol_used_in_source_asm(
    const MinicC0Program *program, const MinicProgramSymbolUses *uses,
    const char *name) {
    size_t index;
    for (index = 0U; index < program->file_asm_count; ++index)
        if (minic_program_asm_names_symbol(
                program->file_asms[index].text, program->file_asms[index].length, name))
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
static inline bool minic_program_emit_collect_symbol_uses(
    MinicProgramSymbolUses *uses, const MinicC0Program *program,
    const MinicCoreFunction *functions, size_t function_count,
    const MinicProgramEmitConfig *config) {
    size_t index, ordinal, block_id;
    for (index = 0U; index < program->global_object_count; ++index) {
        const MinicGlobalObject *object = &program->global_objects[index];
        if (!minic_c0_program_global_object_emittable(program, object))
            continue;
        if (object->alias_target < program->global_object_count &&
            !minic_program_symbol_use_add(
                uses, program->global_objects[object->alias_target].name))
            return false;
        for (ordinal = 0U; ordinal < object->relocation_count; ++ordinal) {
            const MinicGlobalRelocation *reloc = &object->relocations[ordinal];
            if (reloc->target_kind == MINIC_GLOBAL_RELOCATION_OBJECT &&
                reloc->target_id < program->global_object_count &&
                !minic_program_symbol_use_add(
                    uses, program->global_objects[reloc->target_id].name))
                return false;
            if (reloc->target_kind == MINIC_GLOBAL_RELOCATION_FUNCTION &&
                reloc->target_id < program->function_count) {
                const MinicFunction *function = &program->functions[reloc->target_id];
                if (!minic_program_symbol_use_add(
                        uses, function->assembler_name != NULL ?
                        function->assembler_name : function->name))
                    return false;
            }
        }
    }
    for (index = 0U; index < program->function_count; ++index) {
        const MinicFunction *function = &program->functions[index];
        if (function->alias_target < program->function_count) {
            const MinicFunction *target = &program->functions[function->alias_target];
            if (!minic_program_symbol_use_add(
                    uses, target->assembler_name != NULL ?
                    target->assembler_name : target->name))
                return false;
        }
    }
    for (index = 0U; index < function_count; ++index) {
        const MinicCoreFunction *function = &functions[index];
        bool *reachable = NULL;
        if (!program->functions[index].is_defined ||
            !minic_program_function_selected(&program->functions[index], config->selection))
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
 * Definitions and aliases already emit their own visibility directives. */
static inline bool minic_program_emit_undefined_symbol_visibility(
    FILE *out, const MinicC0Program *program,
    const MinicCoreFunction *functions, size_t function_count,
    const MinicProgramEmitConfig *config) {
    size_t index;
    MinicProgramSymbolUses uses = {0};
    bool ok = true;
    if (out == NULL || program == NULL) return false;
    if (!minic_program_emit_collect_symbol_uses(
            &uses, program, functions, function_count, config)) {
        free(uses.names);
        free(uses.asm_uses);
        return false;
    }
    for (index = 0U; index < program->global_object_count; ++index) {
        const MinicGlobalObject *object = &program->global_objects[index];
        if (object->is_extern && !object->is_internal &&
            object->alias_target == MINIC_GLOBAL_OBJECT_INVALID) {
            const char *directive = minic_multi_elf_alias_visibility(object->visibility);
            if (object->visibility != MINIC_SYMBOL_VISIBILITY_DEFAULT &&
                (minic_program_symbol_use_has(&uses, object->name) ||
                 minic_program_symbol_used_in_source_asm(program, &uses, object->name)) &&
                (directive == NULL || object->name == NULL ||
                 fprintf(out, "%s %s\n", directive, object->name) < 0)) {
                ok = false;
                break;
            }
        }
    }
    for (index = 0U; ok && index < program->function_count; ++index) {
        const MinicFunction *function = &program->functions[index];
        if (!function->is_defined && !function->is_internal &&
            function->alias_target == MINIC_FUNCTION_INVALID) {
            const char *directive = minic_multi_elf_alias_visibility(function->visibility);
            const char *name = function->assembler_name != NULL
                ? function->assembler_name : function->name;
            if (function->visibility != MINIC_SYMBOL_VISIBILITY_DEFAULT &&
                (minic_program_symbol_use_has(&uses, name) ||
                 minic_program_symbol_used_in_source_asm(program, &uses, name)) &&
                (directive == NULL || name == NULL ||
                 fprintf(out, "%s %s\n", directive, name) < 0)) {
                ok = false;
                break;
            }
        }
    }
    free(uses.names);
    free(uses.asm_uses);
    return ok;
}

static inline bool minic_program_emit_write(
    const char *path, const MinicC0Program *program,
    const MinicTargetInfo *target, const MinicCoreFunction *functions,
    size_t function_count, bool function_sections,
    const MinicProgramEmitConfig *config, void *context) {
    FILE *out;
    char *temporary;
    size_t path_length;
    int descriptor;
    bool ok;
    bool functions_ok;
    if (path == NULL || program == NULL || target == NULL ||
        config == NULL || config->emit_global == NULL ||
        config->emit_function == NULL ||
        function_count != program->function_count ||
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
         config->prefix(out, program, target, functions, function_count,
                        function_sections, context);
    if (ok) ok = minic_program_emit_undefined_symbol_visibility(
        out, program, functions, function_count, config);
    if (ok)
        ok = minic_program_visit_globals(
            out, program, target, config->emit_global, context);
    if (ok && config->between != NULL)
        ok = config->between(out, program, target, functions, function_count,
                             function_sections, context);
    if (ok || config->continue_after_failure) {
        functions_ok = minic_program_visit_prepared_functions(
            out, program, target, functions, function_count,
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

#endif
