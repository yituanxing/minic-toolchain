#ifndef MINIC_TARGET_COMMON_DIRECT_INSTRUCTION_SEMANTICS_H
#define MINIC_TARGET_COMMON_DIRECT_INSTRUCTION_SEMANTICS_H

#include "target/common/direct_call_plan.h"
#include "target/common/lp64_frame.h"

/* Only ISA-independent, already-shared AAPCS64/LP64D Core admission.
 * Machine addressing, byte transfers, registers, and call/return ABI
 * stay independently owned by each target emitter. */
bool minic_multi_direct_record_copy_extent(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    size_t *bytes);

bool minic_multi_direct_parameter_object_location(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    const MinicLP64ArgumentPlan *incoming_plan,
    const MinicLP64FramePlan *frame_plan,
    const MinicLP64ArgumentLocation **location, size_t *object_offset);

/* Source/Core instruction-value admission shared by AAPCS64 and LP64D.
 * The bool preserves the one documented double-varargs ISA difference. */
bool minic_multi_direct_result_slot_offset(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    bool allow_variadic_double_result, size_t *offset);

bool minic_multi_direct_scalar_store_shape(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    size_t *width, bool *is_signed, bool *is_pointer);

#endif
