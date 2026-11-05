#ifndef MINIC_TARGET_RISCV64_FUNCTION_ABI_PLAN_H
#define MINIC_TARGET_RISCV64_FUNCTION_ABI_PLAN_H
#include "core/core_ir.h"
#include "target/riscv64/abi.h"
#include "target/riscv64/frame_requirements.h"
#include <stdbool.h>
#include <stddef.h>
/* Function-lifetime RV64 psABI classification: one owner for admissibility
 * and physical parameter placement. Machine emission does no reclassification. */
typedef struct MinicRiscv64FunctionAbiPlan {
    MinicRiscv64AbiValue return_value;
    MinicRiscv64AbiCursor named_parameter_cursor;
    MinicRiscv64AbiArgumentLocation *parameter_locations;
    size_t parameter_location_count;
    /* The exact Common CALL index supplies the one outgoing psABI plan;
     * admission validates it, then the frame takes ownership for emission. */
    MinicRiscv64CallPlanCache outgoing_calls;
    bool prepared;
} MinicRiscv64FunctionAbiPlan;
/* Borrowed, immutable ABI facts for Core->MachineIR. A prepared plan owns
 * these allocations until consumed by the old frame builder; afterwards the
 * finalized frame owns the SAME allocations. Never classify or clone ABI
 * locations in a selector. This view must not outlive its current owner. */
typedef struct MinicRiscv64FunctionAbiView {
    MinicRiscv64AbiValue return_value;
    MinicRiscv64AbiCursor named_parameter_cursor;
    const MinicRiscv64AbiArgumentLocation *parameter_locations;
    size_t parameter_location_count;
    const MinicRiscv64CallPlanCache *outgoing_calls;
    bool validated;
} MinicRiscv64FunctionAbiView;
bool minic_riscv64_function_abi_view_from_prepared(
    const MinicRiscv64FunctionAbiPlan *, MinicRiscv64FunctionAbiView *);
bool minic_riscv64_function_abi_plan_build_profile(
    const MinicCoreFunction *,
    MinicRiscv64FunctionAbiPlan *, bool lp64_soft_float);
bool minic_riscv64_function_abi_plan_build(
    const MinicCoreFunction *,
    MinicRiscv64FunctionAbiPlan *);
void minic_riscv64_function_abi_plan_destroy(MinicRiscv64FunctionAbiPlan *);
#endif
