#ifndef MINIC_TARGET_LOONGARCH64_MACHINE_TARGET_STATE_H
#define MINIC_TARGET_LOONGARCH64_MACHINE_TARGET_STATE_H

#include "target/target_info.h"

#include <string.h>

typedef struct MinicLoongarch64TargetMachineState {
    bool soft_float_abi;
} MinicLoongarch64TargetMachineState;

_Static_assert(sizeof(MinicLoongarch64TargetMachineState) <=
               sizeof(((MinicTargetInfo *)0)->machine_state),
               "LoongArch64 target options exceed TargetInfo snapshot capacity");

static inline bool minic_loongarch64_target_soft_float_abi(
    const MinicTargetInfo *target) {
    MinicLoongarch64TargetMachineState state = {0};
    if (target != NULL && target->architecture == MINIC_TARGET_ARCH_LOONGARCH64)
        memcpy(&state, target->machine_state, sizeof(state));
    return state.soft_float_abi;
}

static inline bool minic_loongarch64_target_soft_float_abi_set(
    MinicTargetInfo *target, bool enabled) {
    MinicLoongarch64TargetMachineState state = {0};
    if (target == NULL || target->architecture != MINIC_TARGET_ARCH_LOONGARCH64)
        return false;
    state.soft_float_abi = enabled;
    memcpy(target->machine_state, &state, sizeof(state));
    return true;
}

#endif
