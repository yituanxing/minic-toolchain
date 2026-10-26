#include "target/riscv/isa_resolve.h"
#include "target/target_catalog.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static bool reject(char *why, size_t cap, const char *message) {
    if (why != NULL && cap > 0U) (void)snprintf(why, cap, "%s", message);
    return false;
}

/* Target ISA availability, codegen choices and psABI are independent.
 * Unknown ISA spellings fail closed; source-asm supersets remain Base-I. */
bool minic_riscv_isa_resolve(const char *march, const char *mabi,
                             MinicRiscvIsa *isa, char *reason, size_t capacity) {
    MinicRiscvIsa resolved;
    const MinicRiscvProfile *profile = minic_riscv_profile_find(march);
    if (isa == NULL || (isa->xlen != 64U && isa->xlen != 32U))
        return reject(reason, capacity, "unsupported RISC-V XLEN");
    if (isa->xlen == 32U) {
        if (march != NULL &&
            (profile == NULL || profile->kind != MINIC_RISCV_PROFILE_RV32I))
            return reject(reason, capacity, "RV32 currently accepts rv32i only");
        if (mabi != NULL && strcmp(mabi, "ilp32") != 0)
            return reject(reason, capacity, "rv32i requires ilp32");
        resolved = *isa;
        resolved.abi = MINIC_RISCV_ABI_ILP32;
        resolved.features = 0U;
        resolved.codegen_features = 0U;
        resolved.strict_isa = true;
        *isa = resolved;
        return true;
    }
    if (march == NULL) {
        if (mabi == NULL || strcmp(mabi, "lp64d") == 0) return true;
        return reject(reason, capacity,
                      "explicit -march is required for non-default RISC-V ABI");
    }
    if (profile == NULL || profile->xlen != 64U)
        return reject(reason, capacity,
                      "supported explicit -march: rv64i, rv64im, rv64imafd, "
                      "rv64ima_zicsr_zifencei, rv64imac_zicsr_zifencei; "
                      "other ISA combinations are not yet qualified");
    if (mabi == NULL) mabi = profile->mabi;
    if (strcmp(mabi, "lp64") != 0 &&
        !(strcmp(mabi, "lp64d") == 0 &&
          (profile->features & (MINIC_RISCV_FEATURE_F | MINIC_RISCV_FEATURE_D)) ==
              (MINIC_RISCV_FEATURE_F | MINIC_RISCV_FEATURE_D)))
        return reject(reason, capacity,
                      "ISA and ABI mismatch: lp64d requires F/D");
    resolved = *isa;
    resolved.abi = strcmp(mabi, "lp64d") == 0 ?
                   MINIC_RISCV_ABI_LP64D : MINIC_RISCV_ABI_LP64;
    resolved.features = profile->features;
    resolved.codegen_features = profile->codegen_features;
    /* The LP64 integer-register FP ABI does not imply LP64D FP calling
     * convention. Retain the qualified soft-float arithmetic fallback. */
    if (resolved.abi == MINIC_RISCV_ABI_LP64)
        resolved.codegen_features &= ~(uint32_t)(MINIC_RISCV_FEATURE_F | MINIC_RISCV_FEATURE_D);
    if ((resolved.codegen_features & ~resolved.features) != 0U)
        return reject(reason, capacity, "compiler ISA exceeds target ISA");
    resolved.strict_isa = true;
    *isa = resolved;
    return true;
}

