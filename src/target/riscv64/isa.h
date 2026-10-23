#ifndef MINIC_TARGET_RISCV64_ISA_H
#define MINIC_TARGET_RISCV64_ISA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* ISA extensions and the psABI are independent. The default RV64 legacy
 * profile remains untouched; explicit -march selects a strictly audited ISA.
 * This is invocation state, never a process-global "current architecture". */
typedef enum MinicRiscvAbi {
    MINIC_RISCV_ABI_INVALID = 0,
    MINIC_RISCV_ABI_LP64,
    MINIC_RISCV_ABI_LP64D
} MinicRiscvAbi;

typedef enum MinicRiscvFeature {
    MINIC_RISCV_FEATURE_M = UINT32_C(1) << 0,
    MINIC_RISCV_FEATURE_A = UINT32_C(1) << 1,
    MINIC_RISCV_FEATURE_F = UINT32_C(1) << 2,
    MINIC_RISCV_FEATURE_D = UINT32_C(1) << 3,
    MINIC_RISCV_FEATURE_C = UINT32_C(1) << 4,
    MINIC_RISCV_FEATURE_ZICSR = UINT32_C(1) << 5,
    MINIC_RISCV_FEATURE_ZIFENCEI = UINT32_C(1) << 6
} MinicRiscvFeature;

typedef struct MinicRiscvIsa {
    unsigned xlen;
    MinicRiscvAbi abi;
    uint32_t features;
    bool strict_isa;
} MinicRiscvIsa;

bool minic_riscv_isa_resolve(const char *march, const char *mabi,
                             MinicRiscvIsa *isa, char *reason, size_t capacity);
bool minic_riscv_isa_has(const MinicRiscvIsa *isa, uint32_t requirements);

/* A small target-owned instruction-family selector. Call only for reachable
 * Core instructions, after frame/ABI planning and before emitting machine
 * bytes. Unknown auxiliary opcodes are still audited in the final assembler
 * stream. This selection has no cost in the historical default path. */
#include "core/core_ir.h"
#include "frontend/ast.h"
bool minic_riscv_isa_admit_core_instruction(
    const MinicRiscvIsa *isa, const MinicCoreFunction *function,
    const MinicCoreInstruction *instruction, char *reason, size_t capacity);

/* LP64 soft-float calls cannot silently use the old LP64D floating register
 * ABI, even when a C function only copies a float without FP arithmetic.
 * Inspect nested record/array argument and return types too. */
bool minic_riscv_isa_soft_float_signature_supported(
    const MinicRiscvIsa *isa, const MinicC0Program *program,
    const MinicFunction *function);

/* Deliberately narrow first LP64 ABI guarantee. Only scalar one-XLEN
 * integer, enum and pointer arguments/results (or void result); no
 * aggregates, soft-float signatures, 128-bit or variadic calls. Applies
 * equally to direct declarations and Core direct/indirect call signatures. */
bool minic_riscv_isa_lp64_integer_signature_supported(
    MinicType return_type, const MinicType *parameter_types,
    size_t parameter_count, bool is_variadic);

/* Opt-in experiment for frozen Linux .i corpus: compiler-owned instructions
 * must remain RV64I; user-provided inline/file asm is provenance tagged and
 * kept verbatim apart from operand substitution. Default strict behavior does
 * NOT accept inline asm. Never use this mode as final ELF ISA certification. */
bool minic_riscv64_asm_audit_mode(void);
bool minic_riscv64_asm_audit_marker(FILE *file, bool begin);

/* End-to-end fail-closed instruction selection audit, over the actual
 * assembler text (including frame, helper and address emission). */
bool minic_riscv64_check_isa_assembly(const char *path,
                                      const MinicRiscvIsa *isa,
                                      char *reason, size_t capacity);
#endif
