#include "target/riscv64/function_abi_plan.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
/* Return-pointer reservation, float/int register cursors and split stack
 * locations are classified once and subsequently passed into the frame. */
bool minic_riscv64_function_abi_plan_build(
    const MinicC0Program *program, const MinicCoreFunction *fn,
    MinicRiscv64FunctionAbiPlan *plan) {
    MinicRiscv64AbiCursor cursor;
    size_t i;
    if (program == NULL || fn == NULL || plan == NULL ||
        plan->prepared || plan->parameter_locations != NULL ||
        (fn->parameter_count != 0U && fn->parameter_types == NULL) ||
        fn->parameter_count > SIZE_MAX / sizeof(*plan->parameter_locations) ||
        !minic_riscv64_abi_cursor_initialize_for_return(
             program, fn->return_type, &cursor, &plan->return_value))
        return false;
    if (fn->parameter_count != 0U) {
        plan->parameter_locations = (MinicRiscv64AbiArgumentLocation *)calloc(
            fn->parameter_count, sizeof(*plan->parameter_locations));
        if (plan->parameter_locations == NULL) return false;
    }
    for (i = 0U; i < fn->parameter_count; ++i) {
        if (!minic_riscv64_abi_place_argument(
                program, fn->parameter_types[i], true, &cursor,
                &plan->parameter_locations[i])) {
            minic_riscv64_function_abi_plan_destroy(plan);
            return false;
        }
    }
    plan->named_parameter_cursor = cursor;
    plan->parameter_location_count = fn->parameter_count;
    plan->prepared = true;
    return true;
}
void minic_riscv64_function_abi_plan_destroy(
    MinicRiscv64FunctionAbiPlan *plan) {
    if (plan == NULL) return;
    free(plan->parameter_locations);
    (void)memset(plan, 0, sizeof(*plan));
}
