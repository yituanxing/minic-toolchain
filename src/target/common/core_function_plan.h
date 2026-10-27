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
    MinicCoreValueId *variadic_results;
    bool prepared;
} MinicCoreFunctionPlan;

void minic_core_function_plan_destroy(MinicCoreFunctionPlan *);
bool minic_core_function_plan_build(
    const MinicC0Program *, const MinicTargetInfo *,
    const MinicCoreFunction *, MinicCoreFunctionPlan *);

#endif
