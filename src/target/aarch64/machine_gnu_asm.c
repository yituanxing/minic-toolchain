#include "target/aarch64/machine_opcode.h"
#include "target/aarch64/machine_source_asm.h"

/* The typed AArch64 opcode table and its physical GNU spelling remain a
 * single target-owned contract.  This entry point binds that contract to the
 * selected immutable target, with no process-global code-generation mode. */
extern bool minic_aarch64_machine_opcode_initialize(
    const MinicTargetInfo *, MinicMachineGNUAsmPolicy *);

bool minic_aarch64_machine_gnu_asm_policy_initialize(
    const MinicTargetInfo *target, MinicMachineGNUAsmPolicy *policy) {
    if (!minic_aarch64_machine_opcode_initialize(target, policy)) return false;
    policy->verify.validate_source_asm =
        minic_aarch64_machine_source_asm_validate;
    policy->emit_source_asm = minic_aarch64_machine_source_asm_emit;
    return true;
}
