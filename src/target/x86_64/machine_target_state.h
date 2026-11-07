#ifndef MINIC_TARGET_X86_64_MACHINE_TARGET_STATE_H
#define MINIC_TARGET_X86_64_MACHINE_TARGET_STATE_H

#include "target/target_info.h"

#include <string.h>

typedef struct MinicX86TargetMachineState {
    unsigned int codegen_options;
} MinicX86TargetMachineState;

_Static_assert(sizeof(MinicX86TargetMachineState) <=
               sizeof(((MinicTargetInfo *)0)->machine_state),
               "x86 target options exceed TargetInfo snapshot capacity");

static inline unsigned int minic_x86_64_target_codegen_options(
    const MinicTargetInfo *target) {
    MinicX86TargetMachineState state = {0};
    if (target != NULL && target->architecture == MINIC_TARGET_ARCH_X86_64)
        memcpy(&state, target->machine_state, sizeof(state));
    return state.codegen_options;
}

static inline bool minic_x86_64_target_codegen_options_set(
    MinicTargetInfo *target, unsigned int options) {
    MinicX86TargetMachineState state = {0};
    if (target == NULL || target->architecture != MINIC_TARGET_ARCH_X86_64)
        return false;
    state.codegen_options = options;
    memcpy(target->machine_state, &state, sizeof(state));
    return true;
}

#endif
