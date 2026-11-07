#ifndef MINIC_TARGET_LOONGARCH64_MACHINE_LLSC_SELECT_H
#define MINIC_TARGET_LOONGARCH64_MACHINE_LLSC_SELECT_H

#include "target/common/direct_block_context.h"
#include "target/common/machine_builder.h"

/* Selected Linux LoongArch LL/SC and atomic RMW source forms. The callbacks
 * transport sealed Core scalar values to/from physical target GPRs; this
 * helper owns the exact SOURCE_ASM bindings, constraints and effects. */
bool minic_loongarch64_machine_select_llsc(
    MinicMachineBuilder *builder,
    const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins,
    bool (*load)(void *, MinicCoreValueId, unsigned int, bool),
    bool (*store)(void *, MinicCoreValueId, unsigned int, size_t),
    void *opaque);

#endif
