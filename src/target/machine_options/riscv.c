#include "target/machine_options/target_policies.h"

#include "target/machine_options/unsupported.h"
#include "target/riscv/isa_resolve.h"
#include "target/riscv/machine_target_state.h"

#include <stdio.h>

static bool riscv_options_resolve(MinicTargetInfo *target,
                                 const char *march, const char *mabi,
                                 char *reason, size_t capacity) {
    char detail[240];
    MinicRiscvIsa isa;
    if (!minic_riscv_target_isa_load(target, &isa)) return false;
    if (!minic_riscv_isa_resolve(march, mabi,
                                &isa, detail, sizeof(detail))) {
        (void)snprintf(reason, capacity,
                       "invalid RISC-V configuration: %.200s", detail);
        return false;
    }
    return minic_riscv_target_isa_store(target, &isa);
}

bool minic_machine_options_riscv32_resolve(MinicTargetInfo *target,
                                           const char *march, const char *mabi,
                                           char *reason, size_t capacity) {
    return riscv_options_resolve(target, march, mabi, reason, capacity);
}

bool minic_machine_options_riscv64_resolve(MinicTargetInfo *target,
                                           const char *march, const char *mabi,
                                           char *reason, size_t capacity) {
    return riscv_options_resolve(target, march, mabi, reason, capacity);
}

bool minic_machine_options_riscv32_codegen(MinicTargetInfo *target,
                                           const char *option,
                                           char *reason, size_t capacity) {
    (void)target;
    (void)option;
    return minic_machine_options_reject_codegen(reason, capacity);
}

bool minic_machine_options_riscv64_codegen(MinicTargetInfo *target,
                                           const char *option,
                                           char *reason, size_t capacity) {
    (void)target;
    (void)option;
    return minic_machine_options_reject_codegen(reason, capacity);
}
