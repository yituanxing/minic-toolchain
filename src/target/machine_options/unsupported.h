#ifndef MINIC_TARGET_MACHINE_OPTIONS_UNSUPPORTED_H
#define MINIC_TARGET_MACHINE_OPTIONS_UNSUPPORTED_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/* Default admission policy for machine targets that do not implement the
 * corresponding option class. Preserve diagnostic compatibility while
 * allowing the target registry to supply richer policies in other modules. */
static inline bool minic_machine_options_reject_isa(
    char *reason, size_t capacity) {
    (void)snprintf(reason, capacity,
                   "-march/-mabi currently require RISC-V");
    return false;
}

static inline bool minic_machine_options_reject_codegen(
    char *reason, size_t capacity) {
    (void)snprintf(reason, capacity,
                   "x86 mitigation options require x86-64 target");
    return false;
}

#endif
