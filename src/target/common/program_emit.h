#ifndef MINIC_TARGET_COMMON_PROGRAM_EMIT_H
#define MINIC_TARGET_COMMON_PROGRAM_EMIT_H

#include "target/common/program_visit.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/* A single Common output transaction for all five machine targets.
 * Callbacks retain physical ISA/ABI and GNU directive policy ownership. */
typedef bool (*MinicProgramEmitStage)(
    FILE *, const MinicCoreModule *, const MinicTargetInfo *,
    const MinicCoreFunction *, size_t, bool, void *);
typedef bool (*MinicProgramEmitFinalize)(
    const char *, const MinicTargetInfo *, void *);

typedef struct MinicProgramEmitConfig {
    MinicProgramVisitPreparedFunction emit_function;
    MinicProgramEmitStage prefix;
    MinicProgramEmitStage between;
    const char *trailer;
    MinicProgramEmitFinalize finalize;
    MinicProgramFunctionSelection selection;
    bool continue_after_failure;
} MinicProgramEmitConfig;

/* Public module writers and the backend facade share one admission check:
 * complete Core Module validity plus matching live-function type ownership.
 * This runs once before any target-specific planning or output transaction. */
bool minic_program_emit_module_preflight(
    const MinicCoreModule *module, const MinicCoreFunction *functions,
    size_t function_count);

/* Source ASM is copied only from a finalized, sealed ModuleIR. */
bool minic_program_emit_source_file_asm_module(
    FILE *out, const MinicCoreModule *module);

/* Output remains private until all target stages, close and finalization
 * succeed. Failure never replaces an earlier complete assembly file. */
bool minic_program_emit_write_module(
    const char *path, const MinicCoreModule *module,
    const MinicTargetInfo *target, const MinicCoreFunction *functions,
    size_t function_count, bool function_sections,
    const MinicProgramEmitConfig *config, void *context,
    MinicProgramVisitModuleGlobal visit_module_global);

#endif
