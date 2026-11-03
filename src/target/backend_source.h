#ifndef MINIC_TARGET_BACKEND_SOURCE_H
#define MINIC_TARGET_BACKEND_SOURCE_H

/* The source-language target contract is deliberately separate from the
 * sealed Core -> machine emitter. GNU C operands, constraints, asm-goto and
 * source clone policy must never enter the machine-backend descriptor. */
#include "core/core_ir.h"
#include "target/target_info.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct MinicC0Program MinicC0Program;
typedef struct MinicInlineAsm MinicInlineAsm;

/* Source -> Core GNU operand facts. Core transports typed value/address roles,
 * while the selected machine emitter independently verifies native forms. */
typedef struct MinicAsmOperandPlan {
    MinicAsmOperandPlanOwner owner;
    uint64_t source_form_id; /* Source provenance, never an opcode cache key. */
    uint32_t target_machine_form_id;
    uint32_t target_machine_form_variant;
    size_t operand_count;
    size_t output_count;
    MinicCoreStructuredInlineAsmOperandKind kinds[MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT];
    bool gnu_early_clobber[MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT];
    bool gnu_readwrite[MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT];
    bool has_tied_output[MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT];
    size_t tied_output_index[MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT];
    bool explicit_memory_clobber;
    bool implicit_memory_effect;
    bool effective_memory_effect;
} MinicAsmOperandPlan;

typedef MinicAsmOperandPlan MinicTargetInlineAsmContract;

/* Source capture is independent of the ISA recognizer. On false, the plan
 * may be partially mutated and must be discarded, never retried. */
bool minic_source_backend_capture_asm_operand_source(
    const MinicInlineAsm *, MinicAsmOperandPlanOwner, MinicAsmOperandPlan *);
uint64_t minic_source_backend_asm_source_form_id(const MinicInlineAsm *);

typedef bool (*MinicSourceQualifyInlineAsm)(
    const MinicC0Program *, const MinicInlineAsm *, MinicTargetInlineAsmContract *);
/* Source -> normalized equivalence can seal only target machine-form IDs,
 * never alter the already accepted GNU constraints or memory effects. */
typedef bool (*MinicSourceSealNormalizedInlineAsm)(
    const MinicC0Program *, const MinicInlineAsm *, const char *, size_t,
    MinicTargetInlineAsmContract *);
typedef bool (*MinicSourceRequiresQualifiedInputAsm)(const MinicInlineAsm *);
typedef bool (*MinicSourceQualifyInlineAsmGoto)(
    const MinicC0Program *, const MinicInlineAsm *);
typedef bool (*MinicSourceCloneSafeOpaqueAsm)(const MinicInlineAsm *);

typedef struct MinicSourceBackend {
    MinicSourceQualifyInlineAsm qualify_inline_asm;
    MinicSourceSealNormalizedInlineAsm seal_normalized_inline_asm;
    MinicSourceRequiresQualifiedInputAsm requires_qualified_input_asm;
    MinicSourceQualifyInlineAsmGoto qualify_inline_asm_goto;
    MinicSourceCloneSafeOpaqueAsm clone_safe_opaque_asm;
} MinicSourceBackend;

/* Same canonical target inventory as machine dispatch, but an independent
 * source-language policy descriptor. Unknown ISA fails without fallback. */
const MinicSourceBackend *minic_source_backend_for_target(
    const MinicTargetInfo *target);

bool minic_source_backend_qualify_inline_asm(
    const MinicSourceBackend *, const MinicC0Program *, const MinicInlineAsm *,
    MinicTargetInlineAsmContract *);
bool minic_source_backend_seal_normalized_inline_asm(
    const MinicSourceBackend *, const MinicC0Program *, const MinicInlineAsm *,
    const char *, size_t, MinicTargetInlineAsmContract *);
bool minic_source_backend_inline_asm_prefix_matches(
    const MinicInlineAsm *, const char *const *, size_t);
bool minic_source_backend_requires_qualified_input_asm(
    const MinicSourceBackend *, const MinicInlineAsm *);
bool minic_source_backend_qualify_inline_asm_goto(
    const MinicSourceBackend *, const MinicC0Program *, const MinicInlineAsm *);
bool minic_source_backend_clone_safe_opaque_asm(
    const MinicSourceBackend *, const MinicInlineAsm *);

#endif
