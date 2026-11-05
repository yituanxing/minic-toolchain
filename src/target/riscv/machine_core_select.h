#ifndef MINIC_TARGET_RISCV_MACHINE_CORE_SELECT_H
#define MINIC_TARGET_RISCV_MACHINE_CORE_SELECT_H

#include "core/core_ir.h"
#include "target/riscv/machine_frame_select.h"

/* A source-neutral, immutable bridge from each target's FINAL ABI layout to
 * the same RV32I/RV64I native instruction selector. No Core type or C rank
 * is used to decide stack locations; RV32/RV64 planners own those offsets.
 * SIZE_MAX denotes a nonmaterialized pure value or missing object slot. */
typedef struct MinicRiscvCoreMachinePlan {
    const MinicTargetInfo *target;
    const MinicCoreFunction *core;
    const MinicRiscvPhysicalFrame *frame;
    const size_t *value_offsets;
    size_t value_offset_count;
    const size_t *object_offsets;
    size_t object_offset_count;
} MinicRiscvCoreMachinePlan;

/* A real, fail-closed Core→MachineIR selector. This subset supports the
 * complete physical ABI entry/exit/CFG and scalar RV32I/RV64I families. An
 * unsupported Core instruction rejects the WHOLE function; the production
 * driver must never mix this with its old FILE* emitter. */
bool minic_riscv_machine_select_core_subset(MinicMachineFunction *,
    const MinicRiscvCoreMachinePlan *);

#endif
