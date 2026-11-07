#ifndef MINIC_TARGET_LOONGARCH64_MACHINE_UACCESS_SELECT_H
#define MINIC_TARGET_LOONGARCH64_MACHINE_UACCESS_SELECT_H

#include "target/common/direct_block_context.h"
#include "target/common/machine_builder.h"

/* Linux's faulting user-memory load/store and exception-table output are
 * source-authored ISA. This selector owns its physical register bindings;
 * callbacks preserve packed C lvalue alignment independently of user faults.
 */
bool minic_loongarch64_machine_select_uaccess(
    MinicMachineBuilder *builder,
    const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *instruction,
    bool (*load_value)(void *, MinicCoreValueId, unsigned int, bool),
    bool (*read_lvalue)(void *, MinicCoreValueId, unsigned int, size_t),
    bool (*write_lvalue)(void *, MinicCoreValueId, unsigned int, size_t),
    void *opaque);

#endif
