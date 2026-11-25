#include "target/machine_options/target_policies.h"

#include "target/riscv/isa_resolve.h"
#include "target/riscv/machine_target_state.h"

#include <stdio.h>

bool minic_machine_options_riscv_resolve(MinicTargetInfo *target,
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
