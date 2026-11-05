#include "target/loongarch64/machine_opcode.h"

/* Bind a target-owned native opcode/ABI contract to the chosen TargetInfo.
 * This intentionally has no fallback to Core-to-text emission. */
extern bool minic_loongarch64_machine_opcode_initialize(
    const MinicTargetInfo *, MinicMachineGNUAsmPolicy *);

bool minic_loongarch64_machine_gnu_asm_policy_initialize(
    const MinicTargetInfo *target, MinicMachineGNUAsmPolicy *policy) {
    return minic_loongarch64_machine_opcode_initialize(target, policy);
}
