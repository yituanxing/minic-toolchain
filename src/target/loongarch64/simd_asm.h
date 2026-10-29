#ifndef MINIC_TARGET_LOONGARCH64_SIMD_ASM_H
#define MINIC_TARGET_LOONGARCH64_SIMD_ASM_H

#include "target/backend.h"
#include "target/common/direct_function_emit.h"
#include <stdio.h>

/* Exact GNU LSX/LASX ISA contracts for the Linux RAID6 and XOR vector paths.
 * No generic assembler-text passthrough: source constraints and independent
 * machine-side register/memory forms must both prove the complete template. */
bool minic_loongarch64_simd_qualify_source(
    const MinicC0Program *, const MinicInlineAsm *,
    MinicTargetInlineAsmContract *);
bool minic_loongarch64_simd_qualify_opaque(const MinicCoreInlineAsm *);
bool minic_loongarch64_simd_emit_opaque(
    FILE *, const MinicCoreInlineAsm *);
bool minic_loongarch64_simd_emit_structured(
    FILE *, const MinicDirectLP64BlockContext *,
    const MinicCoreInstruction *, const MinicCoreInlineAsm *);

#endif
