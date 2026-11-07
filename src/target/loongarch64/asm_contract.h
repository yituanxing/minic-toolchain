#ifndef MINIC_TARGET_LOONGARCH64_ASM_CONTRACT_H
#define MINIC_TARGET_LOONGARCH64_ASM_CONTRACT_H
#include "target/backend.h"

/* Source admission and typed MachineIR share only sealed IDs and exact
 * template-form recognizers. No old FILE* Core physical emitter remains. */
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
/* Original Linux 6.6 LoongArch static-key patchpoint: source `i` key,
 * qualified Core symbolic relocation and typed Machine successor. */
bool minic_loongarch64_linux_jump_label_asm_goto_core(
    const MinicCoreInlineAsm *source);
#endif
