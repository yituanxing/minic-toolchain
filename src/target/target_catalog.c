#include "target/target_catalog.h"

#include <string.h>

/* Registration does not assert equal backend, assembler and linker maturity.
 * -S delegates preprocessed C to minic-cc for all five registered targets.
 * Only the historical RV64 pipeline has the matching staged PP/AS/LD chain. */
static const MinicTargetCatalogEntry minic_targets[] = {
    {MINIC_TARGET_ARCH_RISCV32, "riscv32-linux",
     MINIC_TARGET_STAGE_ASSEMBLY_FROM_PREPROCESSED_C},
    {MINIC_TARGET_ARCH_RISCV64, "riscv64-linux",
     MINIC_TARGET_STAGE_ASSEMBLY_FROM_PREPROCESSED_C |
     MINIC_TARGET_STAGE_STAGED_ASSEMBLER_AND_LINKER},
    {MINIC_TARGET_ARCH_X86_64, "x86_64-linux",
     MINIC_TARGET_STAGE_ASSEMBLY_FROM_PREPROCESSED_C},
    {MINIC_TARGET_ARCH_AARCH64, "aarch64-linux",
     MINIC_TARGET_STAGE_ASSEMBLY_FROM_PREPROCESSED_C},
    {MINIC_TARGET_ARCH_LOONGARCH64, "loongarch64-linux",
     MINIC_TARGET_STAGE_ASSEMBLY_FROM_PREPROCESSED_C}
};

const MinicTargetCatalogEntry *minic_target_catalog_all(size_t *count) {
    if (count != NULL)
        *count = sizeof(minic_targets) / sizeof(minic_targets[0]);
    return minic_targets;
}

const MinicTargetCatalogEntry *minic_target_catalog_find(const char *name) {
    size_t count, i;
    const MinicTargetCatalogEntry *targets;
    if (name == NULL) return NULL;
    targets = minic_target_catalog_all(&count);
    for (i = 0U; i < count; ++i) {
        if (strcmp(name, targets[i].name) == 0) return &targets[i];
    }
    return NULL;
}

/* Exact qualified ISA spellings only. Extended -S source-asm profiles do
 * not imply built-in MiniAS/LD support. Legacy implicit RV64GC is separate. */
#define M MINIC_RISCV_FEATURE_M
#define A MINIC_RISCV_FEATURE_A
#define F MINIC_RISCV_FEATURE_F
#define D MINIC_RISCV_FEATURE_D
#define C MINIC_RISCV_FEATURE_C
#define CSR MINIC_RISCV_FEATURE_ZICSR
#define FI MINIC_RISCV_FEATURE_ZIFENCEI
static const MinicRiscvProfile minic_riscv_profiles[] = {
    {"rv32i", "ilp32", 32U, MINIC_RISCV_PROFILE_RV32I, 0U, 0U, false},
    {"rv64i", "lp64", 64U, MINIC_RISCV_PROFILE_RV64I, 0U, 0U, true},
    {"rv64im", "lp64", 64U, MINIC_RISCV_PROFILE_RV64IM, M, M, true},
    {"rv64imafd", "lp64d", 64U, MINIC_RISCV_PROFILE_RV64IMAFD,
     M | A | F | D, M | F | D, true},
    /* Source A/CSR/fence.i (and optional C) goes to GNU AS; compiler stays I. */
    {"rv64ima_zicsr_zifencei", "lp64", 64U, MINIC_RISCV_PROFILE_RV64IM,
     M | A | CSR | FI, 0U, false},
    {"rv64imac_zicsr_zifencei", "lp64", 64U, MINIC_RISCV_PROFILE_RV64IM,
     M | A | C | CSR | FI, 0U, false}
};
#undef M
#undef A
#undef F
#undef D
#undef C
#undef CSR
#undef FI

const MinicRiscvProfile *minic_riscv_profile_all(size_t *count) {
    if (count != NULL)
        *count = sizeof(minic_riscv_profiles) / sizeof(minic_riscv_profiles[0]);
    return minic_riscv_profiles;
}

const MinicRiscvProfile *minic_riscv_profile_find(const char *march) {
    size_t i, count;
    const MinicRiscvProfile *profiles;
    if (march == NULL) return NULL;
    profiles = minic_riscv_profile_all(&count);
    for (i = 0U; i < count; ++i) {
        if (strcmp(march, profiles[i].march) == 0) return &profiles[i];
    }
    return NULL;
}
