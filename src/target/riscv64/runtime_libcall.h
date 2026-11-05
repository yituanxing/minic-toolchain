#ifndef MINIC_TARGET_RISCV64_RUNTIME_LIBCALL_H
#define MINIC_TARGET_RISCV64_RUNTIME_LIBCALL_H

#include "target/riscv64/isa.h"
#include "core/core_ir.h"
#include <stdbool.h>

/* Typed MachineIR's compiler-runtime CALL inventory, used to reserve the
 * saved return-address frame slot before physical instruction selection.
 * Runtime instructions themselves are emitted only by MachineIR. */
bool minic_riscv64_needs_machine_runtime_call(
    const MinicRiscvIsa *, const MinicCoreFunction *);

#endif
