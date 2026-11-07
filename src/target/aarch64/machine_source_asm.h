#ifndef MINIC_TARGET_AARCH64_MACHINE_SOURCE_ASM_H
#define MINIC_TARGET_AARCH64_MACHINE_SOURCE_ASM_H

#include "target/common/machine_gnu_asm.h"

/* Paired callbacks: the same target-owned grammar runs before any text is
 * emitted and again for actual GNU substitution. Unsupported forms close. */
bool minic_aarch64_machine_source_asm_validate(
    const MinicTargetInfo *, const MinicMachineInstruction *, void *);
bool minic_aarch64_machine_source_asm_emit(
    FILE *, const MinicMachineFunction *, const MinicMachineInstruction *,
    const MinicMachineGNUAsmLabels *, void *);

#endif
