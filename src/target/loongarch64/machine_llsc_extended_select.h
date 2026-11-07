#ifndef MINIC_TARGET_LOONGARCH64_MACHINE_LLSC_EXTENDED_SELECT_H
#define MINIC_TARGET_LOONGARCH64_MACHINE_LLSC_EXTENDED_SELECT_H

#include "target/common/direct_block_context.h"
#include "target/common/machine_builder.h"

/* Linux LL/SC exception-table and arithmetic source-owned forms. All GNU
 * templates are sealed by the target descriptor and transported as typed
 * SOURCE_ASM bindings. Callbacks own physical register ingress/egress. */
bool minic_loongarch64_machine_select_llsc_extended(
    MinicMachineBuilder *builder,
    const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins,
    bool (*load)(void *, MinicCoreValueId, unsigned int, bool),
    bool (*store)(void *, MinicCoreValueId, unsigned int, size_t),
    bool (*read_lvalue)(void *, MinicCoreValueId, unsigned int, size_t),
    void *opaque);

#endif
