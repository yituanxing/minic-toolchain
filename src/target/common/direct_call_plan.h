#ifndef MINIC_TARGET_COMMON_DIRECT_CALL_PLAN_H
#define MINIC_TARGET_COMMON_DIRECT_CALL_PLAN_H

#include "target/common/core_call_view.h"
#include "target/common/direct_lp64_abi.h"
#include "frontend/ast.h"
#include "target/target_info.h"
#include <stdbool.h>
#include <stddef.h>

/* AAPCS64 and LoongArch64 share a checked CALL bound; the indirect
 * aggregate ABI classifier remains an independent target callback. */
#define MINIC_DIRECT_LP64_MAX_CALL_ARGUMENTS 64U
typedef bool (*MinicDirectLP64IndirectRecord)(
    const MinicC0Program *, const MinicTargetInfo *, MinicType, size_t *);

/* ABI locations are resolved before emission; the target emitter only
 * transfers values to the selected machine locations. */
typedef enum MinicLP64ArgumentKind {
    MINIC_LP64_ARGUMENT_GP,
    MINIC_LP64_ARGUMENT_FP,
    MINIC_LP64_ARGUMENT_WIDE,
    MINIC_LP64_ARGUMENT_RECORD,
    MINIC_LP64_ARGUMENT_INDIRECT_RECORD,
    MINIC_LP64_ARGUMENT_EMPTY_RECORD
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
    size_t instruction_id; /* sorted Core CALL ID */
    size_t argument_count;
    size_t stack_bytes;
    MinicLP64ArgumentLocation arguments[];
} MinicLP64PreparedCall;
typedef struct MinicLP64CallPlanCache {
    size_t call_count;
    MinicLP64PreparedCall **sites; /* one pointer per actual CALL */
    MinicCoreCallIndexHandle semantic_calls; /* borrowed or Common-owned */
} MinicLP64CallPlanCache;
bool minic_lp64_plan_function_calls(
    const MinicC0Program *, const MinicTargetInfo *,
    const MinicCoreFunction *, const MinicCoreCallIndex *,
    const MinicDirectLP64AbiPolicy *,
    MinicDirectLP64IndirectRecord, MinicDirectLP64IndirectRecord,
    const char *architecture_name, const char *function_name,
    size_t *failed_instruction, bool *failed_preflight, bool *has_calls,
    MinicLP64CallPlanCache *);
const MinicLP64PreparedCall *minic_lp64_function_call_plan(
    const MinicLP64CallPlanCache *, size_t instruction_id);
void minic_lp64_call_plan_cache_destroy(MinicLP64CallPlanCache *);

/* Identical argument-location representation for callee ingress. */
bool minic_lp64_plan_incoming_parameters(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicDirectLP64AbiPolicy *abi,
    MinicDirectLP64IndirectRecord indirect_record,
    MinicLP64ArgumentPlan *plan);

#endif
