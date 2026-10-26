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
typedef bool (*MinicBackendEmitProgram)(
    const char *output_path, const MinicC0Program *program,
    const MinicTargetInfo *target, const MinicCoreFunction *functions,
    size_t function_count, bool function_sections,
    MinicDiagnostic *diagnostic);

/* Target selects operand roles; Core materializes their semantic values.
 * No physical registers, ISA opcodes or frame offsets enter Core IR. */
typedef struct MinicTargetInlineAsmContract {
    size_t operand_count;
    MinicCoreStructuredInlineAsmOperandKind kinds[MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT];
} MinicTargetInlineAsmContract;

typedef bool (*MinicBackendQualifyInlineAsm)(
    const MinicC0Program *, const MinicInlineAsm *, MinicTargetInlineAsmContract *);

typedef struct MinicBackend {
    MinicTargetArchitecture architecture;
    const char *name;
    /* This is the only Core-to-machine handoff in the compile driver.
     * Today: direct GNU text. Future: lower, optimize and emit Machine IR. */
    MinicBackendEmitProgram emit_program;
    /* Optional target contract for GNU multi-operand inline assembly. */
    MinicBackendQualifyInlineAsm qualify_inline_asm;
    /* True only when the machine emitter omits unreachable Core blocks.
     * Core call-graph pruning must use exactly the same emitted CFG roots. */
    bool emit_reachable_core_blocks_only;
} MinicBackend;

/* Fail closed on unknown architecture. No default or host fallback. */
const MinicBackend *minic_backend_for_target(const MinicTargetInfo *target);

/* Source-level admission query; absence means defer to existing Core probes. */
bool minic_backend_qualify_inline_asm(
    const MinicBackend *, const MinicC0Program *, const MinicInlineAsm *,
    MinicTargetInlineAsmContract *);

/* Stable driver-facing emission seam. Machine IR can be inserted here. */
bool minic_backend_emit_assembly(
    const MinicBackend *backend, const char *output_path,
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *functions, size_t function_count,
    bool function_sections, MinicDiagnostic *diagnostic);

#endif
