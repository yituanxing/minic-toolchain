#include "target/common/core_function_plan.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Exactly one compiled ABI-neutral function admission and cleanup owner.
 * Backends receive borrowed immutable facts, CALL views and object storage.
 * No machine features, physical stack offsets or register banks live here. */
void minic_core_function_plan_destroy(
    MinicCoreFunctionPlan *plan) {
    if (plan == NULL) return;
    free(plan->variadic_results);
    minic_core_object_storage_plan_destroy(&plan->objects);
    minic_core_call_index_destroy(&plan->calls);
    (void)memset(plan, 0, sizeof(*plan));
}

bool minic_core_function_plan_build(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *function, MinicCoreFunctionPlan *plan) {
    size_t i, variadic = 0U;
    if (plan == NULL || program == NULL || target == NULL ||
        target->data_layout == NULL || function == NULL || plan->prepared ||
        plan->calls.sites != NULL || plan->objects.objects != NULL ||
        plan->variadic_results != NULL)
        return false;
    /* The sole backend-boundary canonical Core verification; every physical
     * target then performs only its own capability and ABI checks. */
    if (!minic_core_function_verify(function) ||
        !minic_core_function_facts_analyze(function, &plan->facts) ||
        plan->facts.direct_call_count >
            SIZE_MAX - plan->facts.indirect_call_count ||
        !minic_core_call_index_build(
            function, plan->facts.direct_call_count +
                      plan->facts.indirect_call_count, &plan->calls) ||
        !minic_core_object_storage_plan_build(
            program, target->data_layout, function, &plan->objects)) {
        minic_core_function_plan_destroy(plan);
        return false;
    }
    /* Only functions with a va_start site pay for a second Core walk.
     * The common visitor owns this index; x86 consumes it without rescanning.
     * No target ABI or register assumptions enter this function. */
    if (plan->facts.variadic_address_count != 0U) {
        if (plan->facts.variadic_address_count >
            SIZE_MAX / sizeof(*plan->variadic_results)) {
            minic_core_function_plan_destroy(plan);
            return false;
        }
        plan->variadic_results = calloc(plan->facts.variadic_address_count,
                                       sizeof(*plan->variadic_results));
        if (plan->variadic_results == NULL) {
            minic_core_function_plan_destroy(plan);
            return false;
        }
        for (i = 0U; i < function->instruction_count; ++i) {
            const MinicCoreInstruction *ins = &function->instructions[i];
            if (ins->kind != MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS)
                continue;
            if (variadic >= plan->facts.variadic_address_count) {
                minic_core_function_plan_destroy(plan);
                return false;
            }
            plan->variadic_results[variadic++] = ins->result;
        }
        if (variadic != plan->facts.variadic_address_count) {
            minic_core_function_plan_destroy(plan);
            return false;
        }
    }
    plan->facts.variadic_address_results = plan->variadic_results;
    plan->facts.call_index = &plan->calls;
    plan->facts.verified_function = function;
    plan->prepared = true;
    return true;
}
