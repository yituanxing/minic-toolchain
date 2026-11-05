#include "target/machine_options/target_policies.h"

#include "target/machine_options/unsupported.h"
#include "target/loongarch64/machine_target_state.h"

#include <stdio.h>
#include <string.h>

bool minic_machine_options_loongarch64_resolve(MinicTargetInfo *target,
                                               const char *march,
                                               const char *mabi,
                                               char *reason, size_t capacity) {
    /* Linux Kbuild can use LP64S while ordinary userspace defaults LP64D.
     * This target does not yet admit a separately qualified -march option. */
    if (march != NULL || mabi == NULL ||
        (strcmp(mabi, "lp64d") != 0 && strcmp(mabi, "lp64s") != 0)) {
        (void)snprintf(reason, capacity,
                       "LoongArch requires -mabi=lp64d|lp64s without -march");
        return false;
    }
    return minic_loongarch64_target_soft_float_abi_set(
        target, strcmp(mabi, "lp64s") == 0);
}

bool minic_machine_options_loongarch64_codegen(MinicTargetInfo *target,
                                               const char *option,
                                               char *reason, size_t capacity) {
    (void)target;
    (void)option;
    return minic_machine_options_reject_codegen(reason, capacity);
}
