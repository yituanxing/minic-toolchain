#ifndef MINIC_TARGET_BACKEND_H
#define MINIC_TARGET_BACKEND_H

#include "core/core_ir.h"
#include "minic/compiler.h"
#include "target/target_info.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct MinicCoreModule MinicCoreModule;

/*
 * Core/Target seam: compiler.c owns C semantics and Core IR; the selected
 * backend owns ABI lowering, frame planning, instruction selection and
 * machine emission. Neither physical registers nor frame offsets cross
 * back into Core. A future Machine IR belongs inside this boundary:
 *
 *   Core -> backend lower -> [optional Machine IR + passes] -> emitter
 *
 * Backend callbacks currently emit directly to GNU assembly. Introducing
 * Machine IR must not require a new frontend target dispatch.
 */
/* A fully sealed translation-unit module is the only machine-side
 * data/symbol boundary. Source-C admission precedes this callback. */
typedef bool (*MinicBackendEmitModule)(
    const char *output_path, const MinicTargetInfo *target,
    const MinicCoreFunction *functions,
    size_t function_count, bool function_sections,
    const MinicCoreModule *module, MinicDiagnostic *diagnostic);

typedef struct MinicBackend {
    /* This is the only Core-to-machine handoff in the compile driver.
     * Today: direct GNU text. Future: lower, optimize and emit Machine IR. */
    MinicBackendEmitModule emit_module;
    /* True only when the machine emitter omits unreachable Core blocks.
     * Core call-graph pruning must use exactly the same emitted CFG roots. */
    bool emit_reachable_core_blocks_only;
} MinicBackend;

/* Fail closed on unknown architecture. No default or host fallback. */
const MinicBackend *minic_backend_for_target(const MinicTargetInfo *target);

/* Sole production handoff. There is deliberately no fallback to the C global
 * emitter when a non-NULL sealed module fails target admission. */
bool minic_backend_emit_assembly_with_module(
    const MinicBackend *backend, const char *output_path,
    const MinicTargetInfo *target,
    const MinicCoreFunction *functions, size_t function_count,
    bool function_sections, const MinicCoreModule *module,
    MinicDiagnostic *diagnostic);

#endif
