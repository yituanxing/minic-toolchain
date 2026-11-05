#ifndef MINIC_TARGET_LOONGARCH64_MACHINE_SELECT_H
#define MINIC_TARGET_LOONGARCH64_MACHINE_SELECT_H

#include "target/common/direct_block_context.h"
#include "target/common/machine_builder.h"

/* Native LP64S/LP64D lowering: a target-owned physical instruction selector.
 * This entry point is fail-closed until the entire Core vocabulary has been
 * ported. The production codegen does not register a partial selector. */
bool minic_loongarch64_select_machine(MinicMachineFunction *,
    const MinicDirectLP64BlockContext *);

/* Separately reusable, complete physical frame lowering. Every operation
 * goes through the target opcode contract, never assembler text. */
bool minic_loongarch64_machine_select_prologue(MinicMachineBuilder *,
    const MinicMachineLP64FrameContract *);
bool minic_loongarch64_machine_select_epilogue(MinicMachineBuilder *,
    const MinicMachineLP64FrameContract *);

#endif
