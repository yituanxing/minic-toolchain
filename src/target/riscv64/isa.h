#ifndef MINIC_TARGET_RISCV64_ISA_H
#define MINIC_TARGET_RISCV64_ISA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ISA extensions and the psABI are independent. The default RV64 legacy
 * profile remains untouched; explicit -march selects a strictly audited ISA.
 * This is invocation state, never a process-global "current architecture". */
typedef enum MinicRiscvAbi {
    MINIC_RISCV_ABI_INVALID = 0,
    MINIC_RISCV_ABI_LP64,
    MINIC_RISCV_ABI_LP64D
} MinicRiscvAbi;

typedef enum MinicRiscvFeature {
    MINIC_RISCV_FEATURE_M = UINT32_C(1) << 0,
    MINIC_RISCV_FEATURE_A = UINT32_C(1) << 1,
    MINIC_RISCV_FEATURE_F = UINT32_C(1) << 2,
    MINIC_RISCV_FEATURE_D = UINT32_C(1) << 3,
    MINIC_RISCV_FEATURE_C = UINT32_C(1) << 4,
    MINIC_RISCV_FEATURE_ZICSR = UINT32_C(1) << 5,
    MINIC_RISCV_FEATURE_ZIFENCEI = UINT32_C(1) << 6
} MinicRiscvFeature;

typedef struct MinicRiscvIsa {
    unsigned xlen;
    MinicRiscvAbi abi;
    uint32_t features;
    bool strict_isa;
} MinicRiscvIsa;

bool minic_riscv_isa_resolve(const char *march, const char *mabi,
                             MinicRiscvIsa *isa, char *reason, size_t capacity);
bool minic_riscv_isa_has(const MinicRiscvIsa *isa, uint32_t requirements);

/* End-to-end fail-closed instruction selection audit, over the actual
 * assembler text (including frame, helper and address emission). */
bool minic_riscv64_check_isa_assembly(const char *path,
                                      const MinicRiscvIsa *isa,
                                      char *reason, size_t capacity);
#endif
