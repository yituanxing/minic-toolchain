#include "target/machine_options/target_policies.h"

#include "target/machine_options/unsupported.h"

bool minic_machine_options_aarch64_resolve(MinicTargetInfo *target,
                                           const char *march, const char *mabi,
                                           char *reason, size_t capacity) {
    (void)target;
    (void)march;
    (void)mabi;
    return minic_machine_options_reject_isa(reason, capacity);
}

bool minic_machine_options_aarch64_codegen(MinicTargetInfo *target,
                                           const char *option,
                                           char *reason, size_t capacity) {
    (void)target;
    (void)option;
    return minic_machine_options_reject_codegen(reason, capacity);
}
