#ifndef MINIC_TARGET_COMMON_DIRECT_BLOCK_CONTEXT_H
#define MINIC_TARGET_COMMON_DIRECT_BLOCK_CONTEXT_H
#include "target/common/direct_call_plan.h"
#include "target/common/lp64_frame.h"
#include "core/core_ir.h"
#include "frontend/ast.h"
#include "target/target_info.h"
#include <stdbool.h>
#include <stddef.h>

/* A function-level snapshot passed to the two independently verified LP64
 * machine block emitters. This is not a machine IR or shared opcode emitter:
 * AAPCS64 and LoongArch LP64D keep independent instruction/ABI semantics. */
typedef struct MinicDirectLP64BlockContext {
    const MinicC0Program *program;
    const MinicTargetInfo *target;
    const MinicCoreFunction *fn;
    const MinicLP64FramePlan *frame_plan;
    const MinicLP64ArgumentPlan *incoming_plan;
    const MinicLP64CallPlanCache *outgoing_calls;
    const char *name;
    size_t id;
    size_t frame;
    size_t save_offset;
    size_t indirect_return_offset;
    size_t va_base;
    size_t va_gp;
    size_t va_fp;
    size_t va_count;
    bool has_direct_calls;
    bool has_indirect_return;
    bool dynamic_frame;
} MinicDirectLP64BlockContext;
#endif
