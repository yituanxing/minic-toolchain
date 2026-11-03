#ifndef MINIC_TARGET_LOONGARCH64_SIMD_ASM_H
#define MINIC_TARGET_LOONGARCH64_SIMD_ASM_H

#include "core/core_ir.h"
#include "target/common/direct_function_emit.h"
#include <stdio.h>

/* Target-owned proof of the original GNU SIMD memory effects and the
 * normalized Core machine template. Descriptor IDs 1..19 are reserved by
 * the scalar Linux ASM registry. */
enum { MINIC_LOONG_MACHINE_ASM_SIMD = 20U };
enum {
    MINIC_LOONG_MACHINE_SIMD_LSX = 1U,
    MINIC_LOONG_MACHINE_SIMD_LASX = 2U
};

/* Exact Core-only LSX/LASX machine contracts. The original GNU C operand
 * qualification and normalized seal live in simd_source_policy.h. Neither
 * machine API accepts a mutable frontend Program or C type descriptor. */
bool minic_loongarch64_simd_qualify_opaque(const MinicCoreInlineAsm *);
bool minic_loongarch64_simd_structured_template_requires_typed_id(
    const char *, size_t);
bool minic_loongarch64_simd_emit_opaque(
    FILE *, const MinicCoreInlineAsm *);
bool minic_loongarch64_simd_emit_structured(
    FILE *, const MinicDirectLP64BlockContext *,
    const MinicCoreInstruction *, const MinicCoreInlineAsm *);

#endif
