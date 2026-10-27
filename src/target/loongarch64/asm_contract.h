#ifndef MINIC_TARGET_LOONGARCH64_ASM_CONTRACT_H
#define MINIC_TARGET_LOONGARCH64_ASM_CONTRACT_H
#include "target/backend.h"
#include "target/common/direct_function_emit.h"

/* Target-owned GNU source admission and independent Core machine proof. */
bool minic_loongarch64_qualify_inline_asm(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract);
bool minic_loongarch64_emit_dbar(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins);
#endif
