#ifndef MINIC_TARGET_AARCH64_MACHINE_CORE_SELECT_H
#define MINIC_TARGET_AARCH64_MACHINE_CORE_SELECT_H

#include "target/common/direct_block_context.h"
#include "target/common/machine_ir.h"

/* A deliberately fail-closed physical Core-to-MachineIR selector. It emits an
 * entire AArch64 function or nothing; the production direct emitter remains
 * in service until its last Core/ABI family has a typed lowering. */
bool minic_aarch64_machine_select_core_subset(MinicMachineFunction *,
    const MinicDirectLP64BlockContext *);

#endif
