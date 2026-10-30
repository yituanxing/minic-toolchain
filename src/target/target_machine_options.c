#include "target/target_machine_options.h"

#include "target/riscv/isa_resolve.h"

#include <stdio.h>
#include <string.h>

/* Target dispatch is deliberately on registered ISA-family policy, never on
 * generic C language width (LP64/ILP32). The compiler driver only forwards
 * the options. Add a future ISA family's resolver here or migrate the family
 * table to target-owned hooks; no frontend/Core changes should be required. */
bool minic_target_machine_options_resolve(MinicTargetInfo *invocation_target,
                                          const char *march, const char *mabi,
                                          char *reason, size_t reason_capacity) {
    const MinicTargetCatalogEntry *entries;
    size_t count, index;

    if (reason == NULL || reason_capacity == 0U) return false;
    reason[0] = '\0';
    if (invocation_target == NULL) {
        (void)snprintf(reason, reason_capacity, "invalid target invocation");
        return false;
    }
    if (march == NULL && mabi == NULL) return true;
    entries = minic_target_catalog_all(&count);
    for (index = 0U; index < count; ++index) {
        char detail[240];
        if (entries[index].architecture != invocation_target->architecture)
            continue;
        if (entries[index].architecture == MINIC_TARGET_ARCH_LOONGARCH64) {
            /* Only ABI selection is currently qualified for LoongArch.
             * Keep the userland LP64D descriptor unchanged by default. */
            if (march != NULL || mabi == NULL ||
                (strcmp(mabi, "lp64d") != 0 && strcmp(mabi, "lp64s") != 0)) {
                (void)snprintf(reason, reason_capacity,
                               "LoongArch requires -mabi=lp64d|lp64s without -march");
                return false;
            }
            invocation_target->loongarch64_soft_float_abi =
                strcmp(mabi, "lp64s") == 0;
            return true;
        }
        if (entries[index].isa_family != MINIC_TARGET_ISA_FAMILY_RISCV) {
            (void)snprintf(reason, reason_capacity,
                           "-march/-mabi currently require RISC-V");
            return false;
        }
        if (!minic_riscv_isa_resolve(march, mabi,
                                    &invocation_target->riscv_isa,
                                    detail, sizeof(detail))) {
            (void)snprintf(reason, reason_capacity,
                           "invalid RISC-V configuration: %.200s", detail);
            return false;
        }
        return true;
    }
    (void)snprintf(reason, reason_capacity, "unregistered target invocation");
    return false;
}
