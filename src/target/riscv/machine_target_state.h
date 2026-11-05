#ifndef MINIC_TARGET_RISCV_MACHINE_TARGET_STATE_H
#define MINIC_TARGET_RISCV_MACHINE_TARGET_STATE_H

#include "target/target_info.h"
#include "target/riscv/isa_features.h"

#include <string.h>

/* No private pointers into TargetInfo: C struct assignment of an invocation
 * must produce a complete, independent machine-state snapshot. */
_Static_assert(sizeof(MinicRiscvIsa) <=
               sizeof(((MinicTargetInfo *)0)->machine_state),
               "RISC-V ISA state exceeds TargetInfo snapshot capacity");

static inline bool minic_riscv_target_isa_load(
    const MinicTargetInfo *target, MinicRiscvIsa *isa) {
    if (target == NULL || isa == NULL ||
        (target->architecture != MINIC_TARGET_ARCH_RISCV32 &&
         target->architecture != MINIC_TARGET_ARCH_RISCV64)) return false;
    memcpy(isa, target->machine_state, sizeof(*isa));
    return true;
}

static inline MinicRiscvIsa minic_riscv_target_isa(
    const MinicTargetInfo *target) {
    MinicRiscvIsa isa = {0};
    (void)minic_riscv_target_isa_load(target, &isa);
    return isa;
}

static inline bool minic_riscv_target_isa_store(
    MinicTargetInfo *target, const MinicRiscvIsa *isa) {
    if (target == NULL || isa == NULL ||
        (target->architecture != MINIC_TARGET_ARCH_RISCV32 &&
         target->architecture != MINIC_TARGET_ARCH_RISCV64)) return false;
    memcpy(target->machine_state, isa, sizeof(*isa));
    return true;
}

static inline bool minic_riscv32_target_state_initialize(MinicTargetInfo *target) {
    const MinicRiscvIsa isa = {
        .xlen = 32U, .abi = MINIC_RISCV_ABI_ILP32, .strict_isa = true
    };
    return minic_riscv_target_isa_store(target, &isa);
}

static inline bool minic_riscv64_target_state_initialize(MinicTargetInfo *target) {
    const MinicRiscvIsa isa = {
        .xlen = 64U, .abi = MINIC_RISCV_ABI_LP64D, .strict_isa = false
    };
    return minic_riscv_target_isa_store(target, &isa);
}

#endif
