#ifndef MINIC_TARGET_LOONGARCH64_MACHINE_SCALAR_SOURCE_SELECT_H
#define MINIC_TARGET_LOONGARCH64_MACHINE_SCALAR_SOURCE_SELECT_H

#include "target/common/direct_block_context.h"
#include "target/common/machine_builder.h"

/* The six sealed, source-authored scalar hardware families use the same
 * verified Core role -> physical register transport. All GNU template bytes
 * and source occurrence identity remain owned by typed SOURCE_ASM. */
bool minic_loongarch64_machine_select_scalar_source(
    MinicMachineBuilder *builder,
    const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins,
    bool (*load)(void *, MinicCoreValueId, unsigned int, bool),
    bool (*store)(void *, MinicCoreValueId, unsigned int, size_t),
    void *opaque);

#endif
