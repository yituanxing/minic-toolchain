#ifndef MINIC_TARGET_MACHINE_OPTIONS_H
#define MINIC_TARGET_MACHINE_OPTIONS_H

#include "target/target_info.h"

#include <stdbool.h>
#include <stddef.h>

/* The driver transports opaque -march/-mabi spellings. Target registration
 * owns admission and the ISA/ABI resolver; no architecture-specific header or
 * family branch belongs in tools/minic-cc or a frontend/Core component.
 * The target object is an invocation-local copy, never a canonical descriptor.
 */
bool minic_target_machine_options_resolve(MinicTargetInfo *invocation_target,
                                          const char *march, const char *mabi,
                                          char *reason, size_t reason_capacity);

#endif
