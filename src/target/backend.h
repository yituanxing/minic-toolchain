#ifndef MINIC_TARGET_BACKEND_H
#define MINIC_TARGET_BACKEND_H

#include "core/core_ir.h"
#include "frontend/ast.h"
#include "minic/compiler.h"
#include "target/target_info.h"

#include <stdbool.h>
#include <stddef.h>

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
typedef bool (*MinicBackendPrepareProgram)(
    const char *source_path, MinicC0Program *program,
    const MinicTargetInfo *target, MinicDiagnostic *diagnostic);

typedef bool (*MinicBackendEmitProgram)(
    const char *output_path, const MinicC0Program *program,
    const MinicTargetInfo *target, const MinicCoreFunction *functions,
    size_t function_count, bool function_sections,
    MinicDiagnostic *diagnostic);

typedef struct MinicBackend {
    MinicTargetArchitecture architecture;
    const char *name;
    /* Optional: legacy RV64 C0 layout before machine emission.
     * A NULL callback means that the backend plans its own layout. */
    MinicBackendPrepareProgram prepare_program;
    /* This is the only Core-to-machine handoff in the compile driver.
     * Today: direct GNU text. Future: lower, optimize and emit Machine IR. */
    MinicBackendEmitProgram emit_program;
} MinicBackend;

/* Fail closed on unknown architecture. No default or host fallback. */
const MinicBackend *minic_backend_for_target(const MinicTargetInfo *target);

/* Stable driver-facing emission seam. Machine IR can be inserted here. */
bool minic_backend_emit_assembly(
    const MinicBackend *backend, const char *output_path,
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *functions, size_t function_count,
    bool function_sections, MinicDiagnostic *diagnostic);

#endif
