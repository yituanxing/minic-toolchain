#ifndef MINIC_TARGET_COMMON_DIRECT_CALL_PLAN_H
#define MINIC_TARGET_COMMON_DIRECT_CALL_PLAN_H

#include "target/common/direct_call_preflight.h"
#include "target/common/lp64_frame.h"
#include <stdbool.h>
#include <stddef.h>

#define MINIC_LP64_MAX_CALL_ARGUMENTS 64U

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

typedef struct MinicLP64DirectCallPlan {
    size_t argument_count, stack_slots, stack_bytes;
    MinicLP64ArgumentLocation arguments[MINIC_LP64_MAX_CALL_ARGUMENTS];
} MinicLP64DirectCallPlan;

/* Only previously admitted Core CALL/INDIRECT_CALL shapes. Explicitly keep
 * ABI differences: AAPCS64 whole-record spill vs LP64D split record,
 * unnamed variadic GP promotion and binary128 even-register alignment. */
bool minic_lp64_plan_direct_call(const MinicC0Program *program,
    const MinicTargetInfo *target, const MinicCoreFunction *fn,
    const MinicCoreInstruction *call, MinicLP64DirectFrameABI abi,
    MinicDirectLP64IndirectRecord indirect_record,
    MinicLP64DirectCallPlan *plan);

#endif
