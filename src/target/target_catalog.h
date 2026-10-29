#ifndef MINIC_TARGET_TARGET_CATALOG_H
#define MINIC_TARGET_TARGET_CATALOG_H

#include <stdbool.h>
#include <stddef.h>
#include "target/riscv/isa_features.h"

/* The architecture identifier and spelling catalog is independent of C
 * types, Core IR, ABI layout and machine code. The standalone product driver
 * can consult it without linking the compiler or duplicating an ISA allowlist.
 * TargetInfo remains the single source of C layout and semantic facts. */
typedef enum MinicTargetArchitecture {
    MINIC_TARGET_ARCH_RISCV64 = 0,
    MINIC_TARGET_ARCH_X86_64,
    MINIC_TARGET_ARCH_AARCH64,
    MINIC_TARGET_ARCH_LOONGARCH64,
    MINIC_TARGET_ARCH_RISCV32
} MinicTargetArchitecture;

typedef enum MinicTargetDriverStage {
    MINIC_TARGET_STAGE_ASSEMBLY_FROM_PREPROCESSED_C = 1U << 0,
    MINIC_TARGET_STAGE_STAGED_ASSEMBLER_AND_LINKER = 1U << 1
} MinicTargetDriverStage;

/* Source ISA selection is separate from machine-code generation features.
 * A target without an explicit -march/-mabi parser rejects these options;
 * RISC-V's hardware and MiniC-generated feature sets remain independent. */
typedef enum MinicTargetIsaFamily {
    MINIC_TARGET_ISA_FAMILY_NONE = 0,
    MINIC_TARGET_ISA_FAMILY_RISCV
} MinicTargetIsaFamily;

typedef struct MinicTargetCatalogEntry {
    MinicTargetArchitecture architecture;
    const char *name;
    unsigned int product_driver_stages;
    MinicTargetIsaFamily isa_family;
} MinicTargetCatalogEntry;

const MinicTargetCatalogEntry *minic_target_catalog_find(const char *name);
const MinicTargetCatalogEntry *minic_target_catalog_for_architecture(
    MinicTargetArchitecture architecture);
const MinicTargetCatalogEntry *minic_target_catalog_default(void);
const MinicTargetCatalogEntry *minic_target_catalog_all(size_t *count);

/* One source of accepted RISC-V ISA/ABI pairs for the standalone driver,
 * full compiler and tests. ISA feature bits remain owned by RISC-V machine
 * legalization, never duplicated in the product driver. */
typedef enum MinicRiscvProfileKind {
    MINIC_RISCV_PROFILE_RV32I = 0,
    MINIC_RISCV_PROFILE_RV64I,
    MINIC_RISCV_PROFILE_RV64IM,
    MINIC_RISCV_PROFILE_RV64IMAFD
} MinicRiscvProfileKind;

typedef struct MinicRiscvProfile {
    const char *march;
    const char *mabi;
    unsigned int xlen;
    MinicRiscvProfileKind kind;
    uint32_t features;
    uint32_t codegen_features;
    bool staged_assembler_qualified;
} MinicRiscvProfile;

const MinicRiscvProfile *minic_riscv_profile_find(const char *march);
const MinicRiscvProfile *minic_riscv_profile_all(size_t *count);

#endif
