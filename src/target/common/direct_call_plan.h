#ifndef MINIC_TARGET_COMMON_DIRECT_CALL_PLAN_H
#define MINIC_TARGET_COMMON_DIRECT_CALL_PLAN_H

#include "target/common/direct_call_preflight.h"
#include "target/common/direct_lp64_abi.h"
#include <stdbool.h>
#include <stddef.h>

/* ABI locations are resolved before emission; the target emitter only
 * transfers values to the selected machine locations. */
typedef enum MinicLP64ArgumentKind {
    MINIC_LP64_ARGUMENT_GP,
    MINIC_LP64_ARGUMENT_FP,
    MINIC_LP64_ARGUMENT_WIDE,
    MINIC_LP64_ARGUMENT_RECORD,
    MINIC_LP64_ARGUMENT_INDIRECT_RECORD
} MinicLP64ArgumentKind;

typedef struct MinicLP64ArgumentLocation {
    MinicLP64ArgumentKind kind;
    size_t gp_first, fp_first, stack_first;
    size_t gp_registers, fp_registers, stack_slots;
    size_t size_bytes, chunks;
} MinicLP64ArgumentLocation;

typedef struct MinicLP64ArgumentPlan {
    size_t argument_count, stack_slots, stack_bytes;
    MinicLP64ArgumentLocation arguments[MINIC_DIRECT_LP64_MAX_CALL_ARGUMENTS];
} MinicLP64ArgumentPlan;

/* Persistent ABI placements sized by each actual call, not 64 padded args. */
typedef struct MinicLP64PreparedCall {
    size_t argument_count;
    size_t stack_bytes;
    MinicLP64ArgumentLocation arguments[];
} MinicLP64PreparedCall;
typedef struct MinicLP64CallPlanCache {
    size_t instruction_count;
    MinicLP64PreparedCall **by_instruction;
} MinicLP64CallPlanCache;
bool minic_lp64_plan_function_calls(
    const MinicC0Program *, const MinicTargetInfo *,
    const MinicCoreFunction *, MinicLP64DirectABI,
    MinicDirectLP64IndirectRecord, MinicLP64CallPlanCache *);
const MinicLP64PreparedCall *minic_lp64_function_call_plan(
    const MinicLP64CallPlanCache *, size_t instruction_id);
void minic_lp64_call_plan_cache_destroy(MinicLP64CallPlanCache *);

/* Only previously admitted Core CALL/INDIRECT_CALL shapes. Explicitly keep
 * ABI differences: AAPCS64 whole-record spill vs LP64D split record,
 * unnamed variadic GP promotion and binary128 even-register alignment. */
bool minic_lp64_plan_direct_call(const MinicC0Program *program,
    const MinicTargetInfo *target, const MinicCoreFunction *fn,
    const MinicCoreInstruction *call, MinicLP64DirectABI abi,
    MinicDirectLP64IndirectRecord indirect_record,
    MinicLP64ArgumentPlan *plan);

/* Identical argument-location representation for callee ingress. */
bool minic_lp64_plan_incoming_parameters(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, MinicLP64DirectABI abi,
    MinicDirectLP64IndirectRecord indirect_record,
    MinicLP64ArgumentPlan *plan);

#endif
