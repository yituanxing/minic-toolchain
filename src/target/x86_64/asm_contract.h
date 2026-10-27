#ifndef MINIC_TARGET_X86_64_ASM_CONTRACT_H
#define MINIC_TARGET_X86_64_ASM_CONTRACT_H
#include "target/backend.h"
bool minic_x86_64_requires_qualified_input_asm(const MinicInlineAsm *);

/* Register allocation, GNU constraints and native instruction templates
 * belong to x86. The shared compiler sees only Core operand roles. */
bool minic_x86_64_qualify_inline_asm(
    const MinicC0Program *, const MinicInlineAsm *, MinicTargetInlineAsmContract *);
/* Qualified x86 per-cpu stable GS segment read, exact GNU p input. */
bool minic_x86_64_asm_percpu_stable_template(const char *, size_t);
bool minic_x86_64_asm_mulacc_template(const char *, size_t);
bool minic_x86_64_asm_muldiv_template(const char *, size_t);
/* The Linux __ffs / __fls 64-bit GNU bit-scan family is ISA-owned.
 * No raw arbitrary-template passthrough or global ASM Core opcode. */
bool minic_x86_64_asm_bit_scan_template(
    const char *text, size_t length, bool *is_reverse);
bool minic_x86_64_asm_matched_template(const char *, size_t, size_t);
#endif
