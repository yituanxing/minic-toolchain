#ifndef MINIC_TARGET_AARCH64_MACHINE_CORE_SELECT_H
#define MINIC_TARGET_AARCH64_MACHINE_CORE_SELECT_H

#include "target/common/direct_block_context.h"
#include "target/common/machine_ir.h"

/* A deliberately fail-closed physical Core-to-MachineIR selector. It emits an
 * entire AArch64 function or nothing; the production direct emitter remains
 * in service until its last Core/ABI family has a typed lowering. */
bool minic_aarch64_machine_select_core_subset(MinicMachineFunction *,
    const MinicDirectLP64BlockContext *);

/* Internal production variant. Only the direct-function emitter may call
 * it, and MUST commit the full physical prepared gate before any GNU ELF
 * envelope. The standalone selector above remains fully verified. */
bool minic_aarch64_machine_select_core_for_production(MinicMachineFunction *,
    const MinicDirectLP64BlockContext *);

/* Independent target Core-operand/ISA proof for a selected Linux GNU ASM
 * occurrence. The common coverage verifier requires it for every seal. */
bool minic_aarch64_machine_prove_selected_source_asm(
    const MinicCoreFunction *, const MinicCoreInstruction *,
    const MinicCoreInlineAsm *, const MinicMachineFunction *,
    const MinicMachineInstruction *, void *);

#endif
