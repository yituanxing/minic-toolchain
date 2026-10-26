#ifndef MINIC_TARGET_RISCV64_ISA_H
#define MINIC_TARGET_RISCV64_ISA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "target/riscv/isa_features.h"

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

/* Source-owned inline/file asm is always provenance tagged (comment-only).
 * MiniC-generated machine instructions are audited independently against
 * selected ISA capabilities. GNU AS is authoritative for source instructions
 * and assembler directives, which may require a wider -march than MiniC's
 * Base-I emitter. Origin audit does not certify a complete ELF/ISA. */
bool minic_riscv64_asm_audit_marker(FILE *file, bool begin);

/* End-to-end fail-closed instruction selection audit, over the actual
 * assembler text (including frame, helper and address emission). */
bool minic_riscv64_check_isa_assembly(const char *path,
                                      const MinicRiscvIsa *isa,
                                      char *reason, size_t capacity);
#endif
