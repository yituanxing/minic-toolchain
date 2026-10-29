#ifndef MINIC_TARGET_AARCH64_ASM_CONTRACT_H
#define MINIC_TARGET_AARCH64_ASM_CONTRACT_H
#include "target/backend.h"
bool minic_aarch64_requires_qualified_input_asm(const MinicInlineAsm *);
/* Original Linux arm64 alternative_has_cap_unlikely asm-goto contract.
 * Source recognition and emitted Core-template recognition remain ISA owned. */
bool minic_aarch64_linux_cap_asm_goto_source(const MinicInlineAsm *);
bool minic_aarch64_linux_cap_asm_goto_core(const MinicCoreInlineAsm *);
bool minic_aarch64_emit_linux_cap_asm_goto(
    FILE *, const MinicCoreInlineAsm *, size_t, size_t);
/* Original Linux static-branch __jump_table PREL32/PREL64 relocation. */
bool minic_aarch64_qualify_inline_asm_goto(
    const MinicC0Program *, const MinicInlineAsm *);
bool minic_aarch64_linux_jump_label_asm_goto_core(const MinicCoreInlineAsm *);
bool minic_aarch64_emit_linux_jump_label_asm_goto(
    FILE *, const MinicCoreInlineAsm *, size_t, size_t);
#include "target/common/direct_block_context.h"
/* Machine-specific template qualification; Core carries operand roles only. */
bool minic_aarch64_qualify_inline_asm(
    const MinicC0Program *, const MinicInlineAsm *, MinicTargetInlineAsmContract *);
bool minic_aarch64_emit_qualified_structured_asm(
    FILE *, const MinicDirectLP64BlockContext *, const MinicCoreInstruction *);
bool minic_aarch64_qualified_opaque_asm(const MinicCoreInlineAsm *);
/* Exact zero-operand Linux KVM basic-asm alternative with feature metadata. */
bool minic_aarch64_linux_barrier_alternative_opaque(
    const MinicCoreInlineAsm *);
bool minic_aarch64_emit_linux_barrier_alternative_opaque(
    FILE *, const MinicCoreInlineAsm *);
/* Bounded GNU .rept N/NOP/.endr from original GICv3 read-ordering erratum. */
unsigned int minic_aarch64_linux_rept_nop_count(const char *, size_t);
/* EL1 DAIF mask update with Linux's exact local_daif_mask annotation. */
unsigned int minic_aarch64_linux_daif_immediate_form(const char *, size_t);
/* Exact privileged Linux PSTATE immediate encodings from original .inst. */
unsigned int minic_aarch64_linux_pstate_imm_opcode(const char *, size_t);
#endif
