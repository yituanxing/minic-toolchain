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
bool minic_riscv64_function_abi_plan_build_profile(
    const MinicC0Program *, const MinicCoreFunction *,
    MinicRiscv64FunctionAbiPlan *, bool lp64_soft_float);
bool minic_riscv64_function_abi_plan_build(
    const MinicC0Program *, const MinicCoreFunction *,
    MinicRiscv64FunctionAbiPlan *);
void minic_riscv64_function_abi_plan_destroy(MinicRiscv64FunctionAbiPlan *);
#endif
