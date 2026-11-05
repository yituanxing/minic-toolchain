#include "target/target_machine_options.h"

#include "target/machine_options/target_policies.h"

#include <stdio.h>

/* Registered target-owned policy callbacks are the only -march/-mabi and
 * codegen-option admission points. This module cannot interpret ISA-family
 * identifiers or know an architecture-specific option spelling. */
typedef struct MinicRegisteredMachineOptions {
    MinicTargetArchitecture architecture;
    bool (*resolve)(MinicTargetInfo *, const char *, const char *,
                    char *, size_t);
    bool (*codegen)(MinicTargetInfo *, const char *, char *, size_t);
} MinicRegisteredMachineOptions;

static const MinicRegisteredMachineOptions registered_policies[] = {
#define MINIC_TARGET_REGISTRATION(arch, id, spelling, stages, isa) \
    {arch, minic_machine_options_##id##_resolve, \
     minic_machine_options_##id##_codegen},
#include "target/target_registration.def"
#undef MINIC_TARGET_REGISTRATION
};

static const MinicRegisteredMachineOptions *machine_options_policy(
    MinicTargetArchitecture architecture) {
    size_t index;
    for (index = 0U;
         index < sizeof(registered_policies) / sizeof(registered_policies[0]);
         ++index) {
        if (registered_policies[index].architecture == architecture)
            return &registered_policies[index];
    }
    return NULL;
}

bool minic_target_machine_options_resolve(MinicTargetInfo *invocation_target,
                                          const char *march, const char *mabi,
                                          char *reason, size_t reason_capacity) {
    const MinicRegisteredMachineOptions *policy;
    if (reason == NULL || reason_capacity == 0U) return false;
    reason[0] = '\0';
    if (invocation_target == NULL) {
        (void)snprintf(reason, reason_capacity, "invalid target invocation");
        return false;
    }
    /* No explicit ISA flags preserve the canonical machine descriptor. */
    if (march == NULL && mabi == NULL) return true;
    policy = machine_options_policy(invocation_target->architecture);
    if (policy == NULL) {
        (void)snprintf(reason, reason_capacity,
                       "unregistered target invocation");
        return false;
    }
    return policy->resolve(invocation_target, march, mabi,
                           reason, reason_capacity);
}

bool minic_target_codegen_option_apply(MinicTargetInfo *invocation_target,
                                       const char *option,
                                       char *reason, size_t reason_capacity) {
    const MinicRegisteredMachineOptions *policy;
    if (reason == NULL || reason_capacity == 0U) return false;
    reason[0] = '\0';
    if (invocation_target == NULL || option == NULL) {
        (void)snprintf(reason, reason_capacity, "invalid machine codegen option");
        return false;
    }
    policy = machine_options_policy(invocation_target->architecture);
    if (policy == NULL) {
        (void)snprintf(reason, reason_capacity,
                       "unregistered target invocation");
        return false;
    }
    return policy->codegen(invocation_target, option, reason, reason_capacity);
}
