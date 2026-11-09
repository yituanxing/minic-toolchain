#ifndef MINIC_TARGET_RISCV_MACHINE_CORE_SELECT_H
#define MINIC_TARGET_RISCV_MACHINE_CORE_SELECT_H

#include "core/core_ir.h"
#include "core/module_ir.h"
#include "target/riscv/machine_frame_select.h"
#include "target/riscv32/rv32_abi_plan.h"
#include "target/riscv64/function_abi_plan.h"

/* A source-neutral, immutable bridge from each target's FINAL ABI layout to
 * the same RV32I/RV64I native instruction selector. No Core type or C rank
 * is used to decide stack locations; RV32/RV64 planners own those offsets.
 * SIZE_MAX denotes a nonmaterialized pure value or missing object slot. */
typedef struct MinicRiscvCoreMachinePlan {
    const MinicTargetInfo *target;
    /* Borrow the sealed TU symbol namespace for PIC preemptibility. Null is
     * permitted only for standalone selectors without a Module owner. */
    const MinicCoreModule *module;
    const MinicCoreFunction *core;
    const MinicRiscvPhysicalFrame *frame;
    /* Exactly one immutable, already-finalized legacy psABI plan. The Core
     * selector only consumes locations; it never computes register cursors.
     * RV64's outgoing cache shares the Common sparse CALL index ordinal. */
    const Rv32FunctionAbiPlan *abi32;
    MinicRiscv64FunctionAbiView abi64_view;
    const MinicRiscv64FunctionAbiView *abi64;
    const MinicCoreCallIndex *semantic_calls64;
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

/* Internal production-only variant: Core selection and coverage remain
 * mandatory, but the physical verifier is performed exactly once by the
 * immediately following production_preflight_prepare before ELF output.
 * Standalone callers MUST use the fully verified selector above. */
bool minic_riscv_machine_select_core_for_production(MinicMachineFunction *,
    const MinicRiscvCoreMachinePlan *);

#endif
