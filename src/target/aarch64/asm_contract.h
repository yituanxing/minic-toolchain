#ifndef MINIC_TARGET_AARCH64_ASM_CONTRACT_H
#define MINIC_TARGET_AARCH64_ASM_CONTRACT_H
#include "target/backend.h"
bool minic_aarch64_requires_qualified_input_asm(const MinicInlineAsm *);
#include "target/common/direct_block_context.h"
/* Machine-specific template qualification; Core carries operand roles only. */
bool minic_aarch64_qualify_inline_asm(
    const MinicC0Program *, const MinicInlineAsm *, MinicTargetInlineAsmContract *);
bool minic_aarch64_emit_qualified_structured_asm(
    FILE *, const MinicDirectLP64BlockContext *, const MinicCoreInstruction *);
bool minic_aarch64_qualified_opaque_asm(const MinicCoreInlineAsm *);
#endif
