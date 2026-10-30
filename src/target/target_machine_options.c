#include "target/target_machine_options.h"

#include "target/riscv/isa_resolve.h"
#include "target/x86_64/codegen_options.h"

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

bool minic_target_codegen_option_apply(MinicTargetInfo *target,
                                       const char *option,
                                       char *reason, size_t capacity) {
    unsigned int flag = 0U;
    bool enabled = true;
    if (reason == NULL || capacity == 0U) return false;
    reason[0] = '\0';
    if (target == NULL || option == NULL) {
        (void)snprintf(reason, capacity, "invalid machine codegen option");
        return false;
    }
    /* The x86 target owns the meaning, while the CLI only transports exact
     * strings. A different ISA must never silently accept x86 mitigation
     * flags or a partially qualified GCC mode. */
    if (target->architecture != MINIC_TARGET_ARCH_X86_64) {
        (void)snprintf(reason, capacity,
                       "x86 mitigation options require x86-64 target");
        return false;
    }
    if (strcmp(option, "-fcf-protection=branch") == 0)
        flag = MINIC_X86_CODEGEN_IBT_ENDBR;
    else if (strcmp(option, "-fcf-protection=none") == 0) {
        flag = MINIC_X86_CODEGEN_IBT_ENDBR;
        enabled = false;
    } else if (strcmp(option, "-mindirect-branch=thunk-extern") == 0)
        flag = MINIC_X86_CODEGEN_INDIRECT_EXTERNAL_THUNK;
    else if (strcmp(option, "-mindirect-branch=keep") == 0) {
        flag = MINIC_X86_CODEGEN_INDIRECT_EXTERNAL_THUNK;
        enabled = false;
    } else if (strcmp(option, "-mfunction-return=thunk-extern") == 0)
        flag = MINIC_X86_CODEGEN_RETURN_EXTERNAL_THUNK;
    else if (strcmp(option, "-mfunction-return=keep") == 0) {
        flag = MINIC_X86_CODEGEN_RETURN_EXTERNAL_THUNK;
        enabled = false;
    } else if (strcmp(option, "-fpatchable-function-entry=16,16") == 0)
        flag = MINIC_X86_CODEGEN_PATCHABLE_ENTRY_16_16;
    else if (strcmp(option, "-fpatchable-function-entry=0,0") == 0 ||
             strcmp(option, "-fpatchable-function-entry=0") == 0) {
        flag = MINIC_X86_CODEGEN_PATCHABLE_ENTRY_16_16;
        enabled = false;
    } else {
        (void)snprintf(reason, capacity, "unsupported x86 codegen option: %.160s",
                       option);
        return false;
    }
    if (enabled) target->x86_codegen_options |= flag;
    else target->x86_codegen_options &= ~flag;
    return true;
}
