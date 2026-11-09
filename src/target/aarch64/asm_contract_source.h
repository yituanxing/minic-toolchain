#ifndef MINIC_TARGET_AARCH64_ASM_CONTRACT_SOURCE_H
#define MINIC_TARGET_AARCH64_ASM_CONTRACT_SOURCE_H

#include "target/aarch64/asm_contract.h"
#include "target/backend_source.h"

/* Only frontend qualification and clone-source safety consult C operands.
 * Native Core form parsing and hardware instruction emission remain in
 * asm_contract.h and must not include C AST or source policy. */
bool minic_aarch64_requires_qualified_input_asm(const MinicInlineAsm *);
bool minic_aarch64_clone_safe_opaque_asm(const MinicInlineAsm *);
bool minic_aarch64_linux_cap_asm_goto_source(const MinicInlineAsm *);
bool minic_aarch64_qualify_inline_asm_goto(
    const MinicTargetInvocation *, const MinicC0Program *, const MinicInlineAsm *);
bool minic_aarch64_qualify_inline_asm(
    const MinicTargetInvocation *, const MinicC0Program *, const MinicInlineAsm *,
    MinicTargetInlineAsmContract *);
bool minic_aarch64_seal_normalized_inline_asm(
    const MinicTargetInvocation *, const MinicC0Program *,
    const MinicInlineAsm *, const char *, size_t,
    MinicTargetInlineAsmContract *);

#endif
