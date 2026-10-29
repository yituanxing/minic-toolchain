#ifndef MINIC_TARGET_BACKEND_H
#define MINIC_TARGET_BACKEND_H

#include "core/core_ir.h"
#include "frontend/ast.h"
#include "minic/compiler.h"
#include "target/target_info.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

/* The Source -> Core -> Target GNU operand plan. The target selects only
 * value/address transport roles and implicit machine effects. The shared
 * source capture fills all GNU qualifiers, ties and semantic identity; Core
 * materializes values without inspecting any ISA register or opcode. */
typedef struct MinicAsmOperandPlan {
    MinicAsmOperandPlanOwner owner;
    /* Stable source-form signature, not a promise of a machine opcode ID.
     * Native emitters must still verify exact target-dependent semantics. */
    uint64_t source_form_id;
    /* Opaque, target-owned semantic machine-form identity and its admitted
     * variant (for example a validated instruction width). Core transports
     * these facts without interpreting any ISA opcode. Zero means legacy
     * source revalidation or generic structured ASM, never 'any opcode'. */
    uint32_t target_machine_form_id;
    uint32_t target_machine_form_variant;
    size_t operand_count;
    size_t output_count;
    MinicCoreStructuredInlineAsmOperandKind kinds[MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT];
    /* GNU physical-register constraint metadata is orthogonal to the Core
     * address/value transport role. For instance an aggregate `=&r` output
     * can use a Core MEMORY_OUTPUT address while its machine register must
     * still be early-clobber. Filled centrally from the accepted source by
     * minic_backend_qualify_inline_asm; individual ISA recognizers must not
     * duplicate constraint parsing. */
    bool gnu_early_clobber[MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT];
    bool gnu_readwrite[MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT];
    bool has_tied_output[MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT];
    size_t tied_output_index[MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT];
    bool explicit_memory_clobber;
    /* Some GNU hardware writes address a scalar pointer register, rather
     * than exposing a GNU +m or +Q operand (Linux arm64 __raw_write*).
     * Preserve the real hardware memory effect in target-neutral Core
     * without misclassifying the pointer as a C memory lvalue. */
    bool implicit_memory_effect;
    bool effective_memory_effect;
} MinicAsmOperandPlan;

/* Existing ISA qualifier signatures remain source-compatible throughout
 * the staged migration; new target forms return the same typed plan. */
typedef MinicAsmOperandPlan MinicTargetInlineAsmContract;

/* Exact GNU source facts, independent of the target qualifier and its
 * possibly address-backed physical-register result representation. */
bool minic_backend_capture_asm_operand_source(
    const MinicInlineAsm *, MinicAsmOperandPlanOwner, MinicAsmOperandPlan *);
uint64_t minic_backend_asm_source_form_id(const MinicInlineAsm *);

typedef bool (*MinicBackendQualifyInlineAsm)(
    const MinicC0Program *, const MinicInlineAsm *, MinicTargetInlineAsmContract *);

/* A target reserves its own input-bearing GNU machine template families.
 * Generic Core knows neither machine opcodes nor physical constraints. */
typedef bool (*MinicBackendRequiresQualifiedInputAsm)(const MinicInlineAsm *);
/* An asm-goto may expose multiple real C control-flow destinations while
 * carrying immediate source operands. Target admission proves its complete
 * machine form; Core owns only the label blocks and resolved C constants. */
typedef bool (*MinicBackendQualifyInlineAsmGoto)(
    const MinicC0Program *, const MinicInlineAsm *);
typedef struct MinicBackend {
    /* This is the only Core-to-machine handoff in the compile driver.
     * Today: direct GNU text. Future: lower, optimize and emit Machine IR. */
    MinicBackendEmitProgram emit_program;
    /* Optional target contract for GNU multi-operand inline assembly. */
    MinicBackendQualifyInlineAsm qualify_inline_asm;
    /* True only when the machine emitter omits unreachable Core blocks.
     * Core call-graph pruning must use exactly the same emitted CFG roots. */
    bool emit_reachable_core_blocks_only;
    /* Target-owned GNU-asm opcode families whose operand constraints must
     * be qualified by the real machine backend BEFORE generic Core lowering.
     * Raw no-operand GNU source asm remains a separate assembly contract. */
    MinicBackendRequiresQualifiedInputAsm requires_qualified_input_asm;
    MinicBackendQualifyInlineAsmGoto qualify_inline_asm_goto;
} MinicBackend;

/* Fail closed on unknown architecture. No default or host fallback. */
const MinicBackend *minic_backend_for_target(const MinicTargetInfo *target);

/* Source-level admission query; absence means defer to existing Core probes. */
bool minic_backend_qualify_inline_asm(
    const MinicBackend *, const MinicC0Program *, const MinicInlineAsm *,
    MinicTargetInlineAsmContract *);

/* Common lexical prefix test only; the target owns the ISA inventory. */
bool minic_backend_inline_asm_prefix_matches(
    const MinicInlineAsm *, const char *const *, size_t);

/* A negative target qualification is authoritative for input-bearing
 * reserved ISA templates: generic Core scalar/opaque lowering must not
 * reinterpret a register operand as an immediate or lose machine effects. */
bool minic_backend_requires_qualified_input_asm(
    const MinicBackend *, const MinicInlineAsm *);
bool minic_backend_qualify_inline_asm_goto(
    const MinicBackend *, const MinicC0Program *, const MinicInlineAsm *);

/* Stable driver-facing emission seam. Machine IR can be inserted here. */
bool minic_backend_emit_assembly(
    const MinicBackend *backend, const char *output_path,
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *functions, size_t function_count,
    bool function_sections, MinicDiagnostic *diagnostic);

#endif
