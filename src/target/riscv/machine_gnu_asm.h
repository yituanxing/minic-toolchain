#ifndef MINIC_TARGET_RISCV_MACHINE_GNU_ASM_H
#define MINIC_TARGET_RISCV_MACHINE_GNU_ASM_H

#include "target/common/machine_gnu_asm.h"
#include "target/riscv/machine_opcode.h"

/* A complete selected-RV32I/RV64I opcode, register, feature, relocation and
 * GNU body serialization policy. Unknown extension opcodes and source ASM
 * without a completed target binding plan fail closed. */
bool minic_riscv_machine_gnu_asm_policy(
    const MinicTargetInfo *target, MinicMachineGNUAsmPolicy *policy);

#endif
