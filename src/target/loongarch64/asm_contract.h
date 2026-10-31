#ifndef MINIC_TARGET_LOONGARCH64_ASM_CONTRACT_H
#define MINIC_TARGET_LOONGARCH64_ASM_CONTRACT_H
#include "target/backend.h"
bool minic_loongarch64_requires_qualified_input_asm(const MinicInlineAsm *);
#include "target/common/direct_function_emit.h"

/* Target-owned GNU source admission and independent Core machine proof. */
bool minic_loongarch64_qualify_inline_asm(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract);
bool minic_loongarch64_emit_qualified_structured_asm(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins);
bool minic_loongarch64_emit_dbar(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins);
bool minic_loongarch64_qualified_opaque_asm(const MinicCoreInlineAsm *);
/* Original Linux 6.6 LoongArch static-key patchpoint: source `i` key,
 * qualified Core symbolic relocation and native __jump_table emission. */
bool minic_loongarch64_qualify_inline_asm_goto(
    const MinicC0Program *program, const MinicInlineAsm *source);
bool minic_loongarch64_linux_jump_label_asm_goto_core(
    const MinicCoreInlineAsm *source);
bool minic_loongarch64_emit_linux_jump_label_asm_goto(
    FILE *out, const MinicCoreInlineAsm *source,
    size_t function_id, size_t function_blocks);
#endif
