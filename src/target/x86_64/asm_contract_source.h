#ifndef MINIC_TARGET_X86_64_ASM_CONTRACT_SOURCE_H
#define MINIC_TARGET_X86_64_ASM_CONTRACT_SOURCE_H

#include "target/x86_64/asm_contract.h"
#include "frontend/type.h"

/* C-language GNU ASM admission. Machine emitters include asm_contract.h
 * and consume only sealed Core forms; they never import MinicType or reopen
 * source C operand constraints. */
bool minic_x86_64_kvm_movdqa_u32x4_type(MinicType);
bool minic_x86_64_linux_mmio_type_matches(MinicType, unsigned int);
bool minic_x86_64_requires_qualified_input_asm(const MinicInlineAsm *);
bool minic_x86_64_qualify_inline_asm(
    const MinicC0Program *, const MinicInlineAsm *,
    MinicTargetInlineAsmContract *);
bool minic_x86_64_qualify_inline_asm_goto(
    const MinicC0Program *, const MinicInlineAsm *);

#endif
