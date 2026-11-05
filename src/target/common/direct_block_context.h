#ifndef MINIC_TARGET_COMMON_DIRECT_BLOCK_CONTEXT_H
#define MINIC_TARGET_COMMON_DIRECT_BLOCK_CONTEXT_H
#include "target/common/direct_call_plan.h"
#include "target/common/core_function_facts.h"
#include "target/common/lp64_frame.h"
#include "target/common/machine_lp64_frame.h"
#include "core/core_ir.h"
#include "core/module_ir.h"
#include "target/target_info.h"
#include <stdbool.h>
#include <stddef.h>

/* A function-level snapshot passed to the two independently verified LP64
 * machine block emitters. This is not a machine IR or shared opcode emitter:
 * AAPCS64 and LoongArch LP64D keep independent instruction/ABI semantics. */
typedef struct MinicDirectLP64BlockContext {
    const MinicCoreModule *module; /* sealed TU ELF linkage/provenance */
    const MinicTargetInfo *target;
    const MinicCoreFunction *fn;
    const MinicCoreFunctionFacts *core_facts; /* visitor-owned immutable view */
    const MinicLP64FramePlan *frame_plan;
    /* Borrowed, immutable single physical ABI/frame contract. The owner is
     * MinicDirectLP64FunctionPlan; both target emitters observe the same
     * finalized facts, never a re-derived or copied scalar snapshot. */
    const MinicMachineLP64FrameContract *machine_frame;
    const MinicLP64ArgumentPlan *incoming_plan;
    const MinicLP64CallPlanCache *outgoing_calls;
    const char *name;
    size_t id;
} MinicDirectLP64BlockContext;
#endif
