#ifndef MINIC_TARGET_COMMON_PROGRAM_EMIT_H
#define MINIC_TARGET_COMMON_PROGRAM_EMIT_H

#include "target/common/program_visit.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

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

static inline bool minic_program_emit_write(
    const char *path, const MinicC0Program *program,
    const MinicTargetInfo *target, const MinicCoreFunction *functions,
    size_t function_count, bool function_sections,
    const MinicProgramEmitConfig *config, void *context) {
    FILE *out;
    bool ok;
    bool functions_ok;
    if (path == NULL || program == NULL || target == NULL ||
        config == NULL || config->emit_global == NULL ||
        config->emit_function == NULL ||
        function_count != program->function_count ||
        (function_count != 0U && functions == NULL))
        return false;
    out = fopen(path, "wb");
    if (out == NULL) return false;
    ok = config->prefix == NULL ||
         config->prefix(out, program, target, functions, function_count,
                        function_sections, context);
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
        ok = config->finalize(path, target, context);
    if (!ok) (void)remove(path);
    return ok;
}

#endif
