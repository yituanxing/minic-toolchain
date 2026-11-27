#include "target/target_machine_options.h"

#include "target/machine_options/target_policies.h"

#include <stdio.h>

/* Registered target-owned policy callbacks are the only -march/-mabi and
 * codegen-option admission points. This module cannot interpret ISA-family
 * identifiers or know an architecture-specific option spelling. */
typedef struct MinicRegisteredMachineOptions {
    bool (*resolve)(MinicTargetInfo *, const char *, const char *,
                    char *, size_t);
    bool (*codegen)(MinicTargetInfo *, const char *, char *, size_t);
} MinicRegisteredMachineOptions;

static const MinicRegisteredMachineOptions registered_policies[] = {
#define MINIC_TARGET_REGISTRATION(arch, id, spelling, stages) \
    MINIC_MACHINE_OPTIONS_##id,
#include "target/target_registration.def"
#undef MINIC_TARGET_REGISTRATION
};

static const MinicRegisteredMachineOptions *machine_options_policy(
    MinicTargetArchitecture architecture, char *reason, size_t reason_capacity) {
    size_t index;
    if (minic_target_catalog_index(architecture,
        sizeof(registered_policies) / sizeof(registered_policies[0]), &index))
        return &registered_policies[index];
    (void)snprintf(reason, reason_capacity, "unregistered target invocation");
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
    policy = machine_options_policy(invocation_target->architecture, reason, reason_capacity);
    if (policy == NULL) return false;
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
    policy = machine_options_policy(invocation_target->architecture, reason, reason_capacity);
    if (policy == NULL) return false;
    return policy->codegen(invocation_target, option, reason, reason_capacity);
}
