#ifndef MINIC_TARGET_LOONGARCH64_MACHINE_BUILTIN_SELECT_H
#define MINIC_TARGET_LOONGARCH64_MACHINE_BUILTIN_SELECT_H

#include "target/common/direct_function_emit.h"
#include "target/common/direct_call_plan.h"
#include "target/common/machine_builder.h"

/* A Core CALL bearing the target-private builtin identity is a native ISA
 * operation, never a symbol BL. The caller has already placed arguments in
 * LP64D a0..a2 and retains ownership of result writeback. This routine
 * rechecks the sealed ISA descriptor, Core call facts and physical ABI slots
 * before selecting a typed opcode plus operand registers/CSR immediate. */
bool minic_loongarch64_machine_select_builtin_call(
    MinicMachineBuilder *, const MinicDirectLP64BlockContext *,
    const MinicCoreInstruction *, const MinicLP64PreparedCall *);

#endif
