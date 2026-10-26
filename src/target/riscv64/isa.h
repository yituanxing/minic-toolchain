#ifndef MINIC_TARGET_RISCV64_ISA_H
#define MINIC_TARGET_RISCV64_ISA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "target/riscv/isa_features.h"

/* A small target-owned instruction-family selector. Call only for reachable
 * Core instructions, after frame/ABI planning and before emitting machine
 * bytes. Unknown auxiliary opcodes are still audited in the final assembler
 * stream. This selection has no cost in the historical default path. */
#include "core/core_ir.h"
#include "frontend/ast.h"
bool minic_riscv_isa_admit_core_instruction(
    const MinicRiscvIsa *isa, const MinicCoreFunction *function,
    const MinicCoreInstruction *instruction, char *reason, size_t capacity);

/* Source-owned inline/file asm is always provenance tagged (comment-only).
 * MiniC-generated machine instructions are audited independently against
 * selected ISA capabilities. GNU AS is authoritative for source instructions
 * and assembler directives, which may require a wider -march than MiniC's
 * Base-I emitter. Origin audit does not certify a complete ELF/ISA. */
bool minic_riscv64_asm_audit_marker(FILE *file, bool begin);

/* Reserved provenance delimiters may be emitted only by MiniC, never by
 * source-owned templates (including file-scope asm). Such templates are
 * rejected before emission so user comments cannot impersonate provenance. */
bool minic_riscv64_source_asm_marker_safe(const char *text, size_t length);

/* End-to-end fail-closed instruction selection audit, over the actual
 * assembler text (including frame, helper and address emission). */
bool minic_riscv64_check_isa_assembly(const char *path,
                                      const MinicRiscvIsa *isa,
                                      char *reason, size_t capacity);
#endif
