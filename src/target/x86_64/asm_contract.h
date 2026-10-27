#ifndef MINIC_TARGET_X86_64_ASM_CONTRACT_H
#define MINIC_TARGET_X86_64_ASM_CONTRACT_H
#include "target/backend.h"

/* Register allocation, GNU constraints and native instruction templates
 * belong to x86. The shared compiler sees only Core operand roles. */
bool minic_x86_64_qualify_inline_asm(
    const MinicC0Program *, const MinicInlineAsm *, MinicTargetInlineAsmContract *);
bool minic_x86_64_asm_mulacc_template(const char *, size_t);
bool minic_x86_64_asm_muldiv_template(const char *, size_t);
bool minic_x86_64_asm_matched_template(const char *, size_t, size_t);
#endif
