#ifndef MINIC_TARGET_MACHINE_OPTIONS_TARGET_POLICIES_H
#define MINIC_TARGET_MACHINE_OPTIONS_TARGET_POLICIES_H

#include "target/machine_options/unsupported.h"

/* Only targets with a distinct machine-option contract implement a callback.
 * The canonical target registration inventory selects these exact policies. */
bool minic_machine_options_riscv_resolve(
    MinicTargetInfo *, const char *, const char *, char *, size_t);
bool minic_machine_options_loongarch64_resolve(
    MinicTargetInfo *, const char *, const char *, char *, size_t);
bool minic_machine_options_x86_64_codegen(
    MinicTargetInfo *, const char *, char *, size_t);

#define MINIC_MACHINE_OPTIONS_riscv32 \
    {minic_machine_options_riscv_resolve, minic_machine_options_reject_codegen}
#define MINIC_MACHINE_OPTIONS_riscv64 MINIC_MACHINE_OPTIONS_riscv32
#define MINIC_MACHINE_OPTIONS_aarch64 \
    {minic_machine_options_reject_isa, minic_machine_options_reject_codegen}
#define MINIC_MACHINE_OPTIONS_loongarch64 \
    {minic_machine_options_loongarch64_resolve, minic_machine_options_reject_codegen}
#define MINIC_MACHINE_OPTIONS_x86_64 \
    {minic_machine_options_reject_isa, minic_machine_options_x86_64_codegen}

#endif
