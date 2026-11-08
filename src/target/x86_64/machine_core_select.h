#ifndef MINIC_TARGET_X86_64_MACHINE_CORE_SELECT_H
#define MINIC_TARGET_X86_64_MACHINE_CORE_SELECT_H

#include "target/x86_64/frame.h"
#include "target/common/machine_ir.h"

/* This selector consumes only sealed Core and the existing physical x86 ABI
 * plan. It creates real selected x86 instructions; unsupported Core effects
 * reject the WHOLE function before a single assembler byte is written. */
bool minic_x86_64_machine_select_core_subset(
    MinicMachineFunction *, const MinicTargetInfo *,
    const MinicCoreFunction *, const MinicX86Frame *);

/* Production-only selection; the mandatory producer-owned prepared gate
 * performs full physical verification before the first ELF envelope byte. */
bool minic_x86_64_machine_select_core_for_production(
    MinicMachineFunction *, const MinicTargetInfo *,
    const MinicCoreFunction *, const MinicX86Frame *);

#endif
