#ifndef MINIC_TARGET_MACHINE_REGISTER_H
#define MINIC_TARGET_MACHINE_REGISTER_H

#include <stdbool.h>
#include <stddef.h>
#include "common/machine_builtin_effect.h"
#include "target/target_catalog.h"

/* Machine-only recognizers. C source policy registers the same functions;
 * one ISA register rule serves both admission and sealed Core emission. */
typedef bool (*MinicMachineRegisterRecognizer)(const char *, size_t);

bool minic_riscv32_machine_fixed_register_read(const char *, size_t);
bool minic_riscv32_machine_local_fixed_register(const char *, size_t);
bool minic_riscv64_machine_fixed_register_read(const char *, size_t);
bool minic_riscv64_machine_local_fixed_register(const char *, size_t);
bool minic_x86_64_machine_fixed_register_read(const char *, size_t);
bool minic_x86_64_machine_local_fixed_register(const char *, size_t);
bool minic_x86_64_machine_register_clobber(const char *, size_t);
bool minic_aarch64_machine_fixed_register_read(const char *, size_t);
bool minic_aarch64_machine_local_fixed_register(const char *, size_t);
struct MinicTargetInfo;
bool minic_aarch64_machine_builtin_form(const struct MinicTargetInfo *,
    const char *, size_t, size_t *, const char **, size_t *);
MinicMachineBuiltinEffect minic_aarch64_machine_builtin_effect(
    const struct MinicTargetInfo *, const char *, size_t);
bool minic_loongarch64_machine_fixed_register_read(const char *, size_t);
bool minic_loongarch64_machine_fixed_register_write(const char *, size_t);
bool minic_loongarch64_machine_local_fixed_register(const char *, size_t);

#endif
