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

/* Declarations with no body/storage nevertheless own ELF binding metadata.
 * GNU AS must see .hidden/.internal/.protected on undefined references before
 * final linking (e.g. vDSO private data). All five backends use this shared
 * stage; defined objects/functions and aliases already emit their directives
 * from their respective definition/alias paths. */
static inline bool minic_program_emit_undefined_symbol_visibility(
    FILE *out, const MinicC0Program *program) {
    size_t index;
    if (out == NULL || program == NULL) return false;
    for (index = 0U; index < program->global_object_count; ++index) {
        const MinicGlobalObject *object = &program->global_objects[index];
        if (object->is_extern && !object->is_internal &&
            object->alias_target == MINIC_GLOBAL_OBJECT_INVALID) {
            const char *directive = minic_multi_elf_alias_visibility(object->visibility);
            if (object->visibility != MINIC_SYMBOL_VISIBILITY_DEFAULT &&
                (directive == NULL || object->name == NULL ||
                 fprintf(out, "%s %s\n", directive, object->name) < 0))
                return false;
        }
    }
    for (index = 0U; index < program->function_count; ++index) {
        const MinicFunction *function = &program->functions[index];
        if (!function->is_defined && !function->is_internal &&
            function->alias_target == MINIC_FUNCTION_INVALID) {
            const char *directive = minic_multi_elf_alias_visibility(function->visibility);
            const char *name = function->assembler_name != NULL
                ? function->assembler_name : function->name;
            if (function->visibility != MINIC_SYMBOL_VISIBILITY_DEFAULT &&
                (directive == NULL || name == NULL ||
                 fprintf(out, "%s %s\n", directive, name) < 0))
                return false;
        }
    }
    return true;
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
    if (ok) ok = minic_program_emit_undefined_symbol_visibility(out, program);
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
