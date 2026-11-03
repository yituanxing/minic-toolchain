#ifndef MINIC_TARGET_LOONGARCH64_ASM_CONTRACT_SOURCE_H
#define MINIC_TARGET_LOONGARCH64_ASM_CONTRACT_SOURCE_H

#include "target/loongarch64/asm_contract.h"
#include "target/backend_source.h"

/* C/GNU inline ASM source policy only. The machine-facing contract retains
 * typed descriptor IDs, exact ISA templates, and Core effect validators. */
bool minic_loongarch64_requires_qualified_input_asm(const MinicInlineAsm *);
bool minic_loongarch64_qualify_inline_asm(
    const MinicTargetInvocation *, const MinicC0Program *, const MinicInlineAsm *,
    MinicTargetInlineAsmContract *);
bool minic_loongarch64_seal_normalized_inline_asm(
    const MinicTargetInvocation *, const MinicC0Program *,
    const MinicInlineAsm *, const char *, size_t,
    MinicTargetInlineAsmContract *);
bool minic_loongarch64_qualify_inline_asm_goto(
    const MinicTargetInvocation *, const MinicC0Program *, const MinicInlineAsm *);

#endif
