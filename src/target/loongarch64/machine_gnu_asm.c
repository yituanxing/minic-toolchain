#include "target/loongarch64/machine_opcode.h"
#include "target/loongarch64/machine_source_asm.h"

/* Bind a target-owned native opcode/ABI contract to the chosen TargetInfo.
 * This intentionally has no fallback to Core-to-text emission. */
extern bool minic_loongarch64_machine_opcode_initialize(
    const MinicTargetInfo *, MinicMachineGNUAsmPolicy *);

bool minic_loongarch64_machine_gnu_asm_policy_initialize(
    const MinicTargetInfo *target, MinicMachineGNUAsmPolicy *policy) {
    if (!minic_loongarch64_machine_opcode_initialize(target, policy))
        return false;
    policy->verify.validate_source_asm =
        minic_loongarch64_machine_validate_source_asm;
    policy->emit_source_asm = minic_loongarch64_machine_emit_source_asm;
    return true;
}
