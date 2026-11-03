#ifndef MINIC_TARGET_LOONGARCH64_ASM_CONTRACT_H
#define MINIC_TARGET_LOONGARCH64_ASM_CONTRACT_H
#include "target/backend.h"
bool minic_loongarch64_requires_qualified_input_asm(const MinicInlineAsm *);
#include "target/common/direct_function_emit.h"

/* Target-owned GNU source admission and independent Core machine proof. */
bool minic_loongarch64_qualify_inline_asm(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract);
/* Post-normalization Source -> Core machine-effect proof. All other
 * previously typed scalar contracts remain unchanged. */
bool minic_loongarch64_seal_normalized_inline_asm(
    const MinicC0Program *, const MinicInlineAsm *,
    const char *, size_t, MinicTargetInlineAsmContract *);
bool minic_loongarch64_emit_qualified_structured_asm(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins);
/* Immutable GNU template + typed descriptor ID/variant machine ingress. */
bool minic_loongarch64_typed_descriptor_matches_template(
    uint32_t id, uint32_t variant, const char *text, size_t length);
/* A descriptor template without its source-owned typed ID must not fall
 * through to any other legacy machine emitter. */
bool minic_loongarch64_descriptor_template_requires_typed_id(
    const char *text, size_t length);
/* The descriptor table owns IDs 1..14; these scalar form payloads are
 * represented entirely by target_machine_form_variant. */
typedef enum MinicLoongLinuxMachineAsmForm {
    MINIC_LOONG_MACHINE_ASM_CLOCK = 15,
    MINIC_LOONG_MACHINE_ASM_IRQ_SWITCH,
    MINIC_LOONG_MACHINE_ASM_INVTLB,
    MINIC_LOONG_MACHINE_ASM_ADD_UNLESS,
    MINIC_LOONG_MACHINE_ASM_FUTEX_RMW,
    /* 20 is the separately sealed LSX/LASX machine family. These source-
     * owned proofs close the remaining scalar Linux hardware forms. */
    MINIC_LOONG_MACHINE_ASM_LITERAL_DBAR = 21,
    MINIC_LOONG_MACHINE_ASM_HINT_DBAR,
    MINIC_LOONG_MACHINE_ASM_SUB_POSITIVE,
    MINIC_LOONG_MACHINE_ASM_UACCESS,
    MINIC_LOONG_MACHINE_ASM_PERCPU_AMO,
    MINIC_LOONG_MACHINE_ASM_CSR,
    MINIC_LOONG_MACHINE_ASM_AMO_STORE,
    MINIC_LOONG_MACHINE_ASM_AMO_FETCH
} MinicLoongLinuxMachineAsmForm;
bool minic_loongarch64_typed_scalar_form_matches_template(
    uint32_t id, uint32_t variant, const char *text, size_t length);
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
