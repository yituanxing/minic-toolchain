#ifndef MINIC_TARGET_LOONGARCH64_MACHINE_BUILTIN_FORM_H
#define MINIC_TARGET_LOONGARCH64_MACHINE_BUILTIN_FORM_H

#include <stdbool.h>
#include <stddef.h>

struct MinicTargetInfo;

/* Target-owned machine spelling, arity and immediate contract. This does
 * not construct a C source type; Source projects its signature separately
 * from the very same LoongArch builtin descriptor table. */
bool minic_loongarch64_machine_builtin_form(
    const struct MinicTargetInfo *, const char *, size_t,
    size_t *, const char **, size_t *);

#endif
