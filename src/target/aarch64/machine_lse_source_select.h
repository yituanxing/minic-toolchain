#ifndef MINIC_TARGET_AARCH64_MACHINE_LSE_SOURCE_SELECT_H
#define MINIC_TARGET_AARCH64_MACHINE_LSE_SOURCE_SELECT_H

#include "target/common/machine_gnu_asm.h"

/* The Linux CASP/CASPAL pair is one sealed source-ASM family.  Its eight
 * GNU operand roles and the five physical fixed-register assignments are
 * checked again after Core->Machine selection, before GNU serialization. */
bool minic_aarch64_machine_lse_casp128_validate(
    const MinicMachineFunction *, const MinicMachineInstruction *);
bool minic_aarch64_machine_lse_casp128_emit(
    FILE *, const MinicMachineFunction *, const MinicMachineInstruction *,
    const MinicMachineGNUAsmLabels *);

/* Linux's 32/64-bit st{clr,set,eor,add}, ld{clr,set,eor,add}, and
 * 8/16/32/64-bit CAS share one exact source grammar and typed role policy. */
bool minic_aarch64_machine_lse_scalar_validate(
    const MinicMachineFunction *, const MinicMachineInstruction *);
bool minic_aarch64_machine_lse_scalar_emit(
    FILE *, const MinicMachineFunction *, const MinicMachineInstruction *,
    const MinicMachineGNUAsmLabels *);

/* Linux atomic_dec_if_positive: one +&r, one +Q and one =&r with NZCV
 * clobber. The retry loop is source-owned, but every physical role is typed. */
bool minic_aarch64_machine_lse_dec_positive_validate(
    const MinicMachineFunction *, const MinicMachineInstruction *);
bool minic_aarch64_machine_lse_dec_positive_emit(
    FILE *, const MinicMachineFunction *, const MinicMachineInstruction *,
    const MinicMachineGNUAsmLabels *);

#endif
