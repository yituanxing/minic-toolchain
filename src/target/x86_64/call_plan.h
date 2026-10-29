#ifndef MINIC_TARGET_X86_64_CALL_PLAN_H
#define MINIC_TARGET_X86_64_CALL_PLAN_H
#include "target/common/core_call_view.h"
#include "target/target_info.h"
#include "frontend/ast.h"
#include <stdbool.h>
#include <stddef.h>

/* The SysV AMD64 6-GPR/8-SSE/x87 call location contract. This is separate
 * from AAPCS64, RV64 and LP64D, which have distinct aggregate policies. */
typedef enum MinicX86ArgClass {
    MINIC_X86_ARG_INTEGER, MINIC_X86_ARG_SSE, MINIC_X86_ARG_X87_MEMORY,
    MINIC_X86_ARG_RECORD_ONE_GP, MINIC_X86_ARG_RECORD_TWO_GP,
    MINIC_X86_ARG_RECORD_MEMORY,
    /* GNU C empty struct: zero-size argument has no SysV register or
     * stack ABI payload and must not shift following arguments. */
    MINIC_X86_ARG_RECORD_EMPTY,
    /* SysV AMD64 classifies __int128 as two INTEGER eightbytes.
     * Both registers must be available; otherwise pass aligned on stack. */
    MINIC_X86_ARG_I128_TWO_GP
} MinicX86ArgClass;

typedef struct MinicX86ArgLocation {
    MinicX86ArgClass kind;
    size_t gp_index, fp_index, stack_index, record_bytes;
    bool on_stack;
} MinicX86ArgLocation;

typedef struct MinicX86ArgCursor {
    size_t gp_used, fp_used, stack_slots;
} MinicX86ArgCursor;

typedef struct MinicX86ArgumentPlan {
    MinicX86ArgLocation *locations;
    size_t argument_count, stack_bytes;
    MinicX86ArgCursor end;
    bool is_variadic;
    /* ABI choice is copied from Core, never inferred from target ISA. */
    MinicFunctionCallingConvention calling_convention;
} MinicX86ArgumentPlan;
/* Sparse SysV outgoing ABI locations, ordered by Common Core CALL ID. */
typedef struct MinicX86OutgoingCallSite {
    size_t instruction_id;
    MinicX86ArgumentPlan plan;
} MinicX86OutgoingCallSite;
typedef struct MinicX86OutgoingCallPlans {
    MinicX86OutgoingCallSite *sites;
    size_t count;
} MinicX86OutgoingCallPlans;

bool minic_x86_64_plan_incoming_arguments(
    const MinicC0Program *, const MinicTargetInfo *,
    const MinicCoreFunction *, MinicX86ArgumentPlan *);
bool minic_x86_64_plan_outgoing_arguments(
    const MinicC0Program *, const MinicTargetInfo *,
    const MinicCoreFunction *, const MinicCoreCallView *,
    MinicX86ArgumentPlan *);
void minic_x86_64_argument_plan_destroy(MinicX86ArgumentPlan *);
bool minic_x86_64_outgoing_call_plans_prepare(
    const MinicC0Program *, const MinicTargetInfo *,
    const MinicCoreFunction *, const MinicCoreCallIndex *,
    MinicX86OutgoingCallPlans *);
void minic_x86_64_outgoing_call_plans_destroy(MinicX86OutgoingCallPlans *);
#endif
