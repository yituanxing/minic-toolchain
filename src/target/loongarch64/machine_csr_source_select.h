#ifndef MINIC_TARGET_LOONGARCH64_MACHINE_CSR_SOURCE_SELECT_H
#define MINIC_TARGET_LOONGARCH64_MACHINE_CSR_SOURCE_SELECT_H

#include "target/common/direct_block_context.h"
#include "target/common/machine_builder.h"

/* Linux privileged CRMD CSR source forms (typed machine form ID26).
 * `load_value` reads a scalar Core SSA value into a GPR.
 * `read_lvalue` reads the CURRENT pointee of a sealed Core pointer, with
 * correct 4/8-byte transport even for packed and runtime-unaligned storage.
 * `write_lvalue` stores the CSR result through that same lvalue, preserving
 * its exact width and any runtime alignment guard/bytewise slow path.
 * `read_lvalue` must leave its result in the nominated register; callbacks
 * may use their own temporary registers but never clobber the result T1
 * between emission and writeback. No FILE* or C-source state crosses here. */
bool minic_loongarch64_machine_select_csr_source(
    MinicMachineBuilder *, const MinicDirectLP64BlockContext *,
    const MinicCoreInstruction *,
    bool (*load_value)(void *, MinicCoreValueId, unsigned int, bool),
    bool (*read_lvalue)(void *, MinicCoreValueId, unsigned int, size_t),
    bool (*write_lvalue)(void *, MinicCoreValueId, unsigned int, size_t),
    void *opaque);

#endif
