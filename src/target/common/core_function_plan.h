#ifndef MINIC_TARGET_COMMON_CORE_FUNCTION_PLAN_H
#define MINIC_TARGET_COMMON_CORE_FUNCTION_PLAN_H

#include "target/common/core_function_facts.h"
#include "target/common/core_object_storage.h"
#include "target/target_info.h"
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* The unique five-target semantic Function Plan for an emitted Core
 * definition. This stage never computes psABI locations, registers,
 * ISA instruction choices or frame offsets. Physical target function
 * plans are strictly downstream consumers of these immutable facts.
 *
 * Lifetime: Common ProgramVisitor owns this plan; target emitters/frames
 * borrow object and CALL storage only while their visitor callback runs. */
typedef struct MinicCoreFunctionPlan {
    MinicCoreFunctionFacts facts;
    MinicCoreCallIndex calls;
    MinicCoreObjectStoragePlan objects;
    bool prepared;
} MinicCoreFunctionPlan;

static inline void minic_core_function_plan_destroy(
    MinicCoreFunctionPlan *plan) {
    if (plan == NULL) return;
    minic_core_object_storage_plan_destroy(&plan->objects);
    minic_core_call_index_destroy(&plan->calls);
    (void)memset(plan, 0, sizeof(*plan));
}

static inline bool minic_core_function_plan_build(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *function, MinicCoreFunctionPlan *plan) {
    if (plan == NULL || program == NULL || target == NULL ||
        target->data_layout == NULL || function == NULL || plan->prepared ||
        plan->calls.sites != NULL || plan->objects.objects != NULL)
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
    plan->facts.call_index = &plan->calls;
    plan->facts.verified_function = function;
    plan->prepared = true;
    return true;
}

#endif
