#ifndef MINIC_TARGET_MACHINE_OPTIONS_TARGET_POLICIES_H
#define MINIC_TARGET_MACHINE_OPTIONS_TARGET_POLICIES_H

#include "target/target_info.h"

#include <stdbool.h>
#include <stddef.h>

/* Machine-option policy belongs to the registered target. The common
 * resolver only dispatches; adding a target requires no frontend, Core IR or
 * generic machine-options conditional branches. */
#define MINIC_TARGET_REGISTRATION(arch, id, spelling, stages, isa) \
    bool minic_machine_options_##id##_resolve( \
        MinicTargetInfo *, const char *, const char *, char *, size_t); \
    bool minic_machine_options_##id##_codegen( \
        MinicTargetInfo *, const char *, char *, size_t);
#include "target/target_registration.def"
#undef MINIC_TARGET_REGISTRATION

#endif
