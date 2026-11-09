#ifndef MINIC_TARGET_LOONGARCH64_MACHINE_PTE_SELECT_H
#define MINIC_TARGET_LOONGARCH64_MACHINE_PTE_SELECT_H

#include "target/common/direct_block_context.h"
#include "target/common/machine_builder.h"

/* Linux LoongArch set_pte buddy-global-bit LL/SC source form.  The sealed
 * Core roles supply a physical memory address, early register output and
 * scalar input; original retry/ordering instructions stay inside the
 * target-qualified SOURCE_ASM template.  The callbacks are selected native
 * MachineIR value transport, never compiler-generated source ASM text. */
bool minic_loongarch64_machine_select_pte(
    MinicMachineBuilder *builder,
    const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins,
    bool (*load)(void *, MinicCoreValueId, unsigned int, bool),
    bool (*store)(void *, MinicCoreValueId, unsigned int, size_t),
    void *opaque);

#endif
