#ifndef MINIC_TARGET_RISCV_ISA_FEATURES_H
#define MINIC_TARGET_RISCV_ISA_FEATURES_H

#include <stdbool.h>
#include <stdint.h>

/* Shared RV32/RV64 machine capability, never GNU assembler policy.
 * The compiler may choose a Base-I fallback for a Core operation even when
 * the caller's -march makes M/C/Z/other extensions available. Source-owned
 * file/inline asm is passed to the assembler as source-owned text. */
/* ISA extensions and the psABI are independent. The default RV64 legacy
 * profile remains untouched; explicit -march selects a strictly audited ISA.
 * This is invocation state, never a process-global "current architecture". */
typedef enum MinicRiscvAbi {
    MINIC_RISCV_ABI_INVALID = 0,
    MINIC_RISCV_ABI_ILP32,
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
    uint32_t features;         /* target/assembler available extensions */
    uint32_t codegen_features; /* MiniC-selected subset: Base-I fallback */
    bool strict_isa;
} MinicRiscvIsa;

static inline bool minic_riscv_codegen_has(const MinicRiscvIsa *isa,
                                            uint32_t requirements) {
    return isa != NULL && (!isa->strict_isa ||
           (isa->codegen_features & requirements) == requirements);
}

#endif
