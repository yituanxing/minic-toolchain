#ifndef MINIC_TARGET_TARGET_CATALOG_H
#define MINIC_TARGET_TARGET_CATALOG_H

#include <stdbool.h>
#include <stddef.h>

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

typedef struct MinicTargetCatalogEntry {
    MinicTargetArchitecture architecture;
    const char *name;
    unsigned int product_driver_stages;
} MinicTargetCatalogEntry;

const MinicTargetCatalogEntry *minic_target_catalog_find(const char *name);
const MinicTargetCatalogEntry *minic_target_catalog_all(size_t *count);

#endif
