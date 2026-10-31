#ifndef MINIC_TARGET_COMMON_DIRECT_RESIDUAL_PLAN_H
#define MINIC_TARGET_COMMON_DIRECT_RESIDUAL_PLAN_H
#include "target/common/lp64_frame.h"
#include "target/common/lp64_scalar.h"
#include "core/core_ir.h"
#include <stdbool.h>
#include <stddef.h>

/* Architecture-neutral operand/layout preparation for the residual Core
 * instruction family. AAPCS64/LP64D still own every emitted instruction. */
typedef struct MinicDirectIntegerCastPlan {
    MinicCoreValueId operand;
    bool source_wide, target_wide;
    /* C conversion to _Bool tests all source bits, never truncates to 1 byte. */
    bool target_boolean;
    size_t source_width, target_width;
    bool source_signed, target_signed;
    size_t source_wide_offset, target_wide_offset;
} MinicDirectIntegerCastPlan;
bool minic_multi_direct_integer_cast_plan(
    const MinicC0Program *, const MinicTargetInfo *,
    const MinicCoreFunction *, const MinicCoreInstruction *,
    const MinicLP64FramePlan *, MinicDirectIntegerCastPlan *);
bool minic_multi_direct_constant_integer_shape(
    const MinicC0Program *, const MinicTargetInfo *,
    const MinicCoreInstruction *, size_t *, bool *);
bool minic_multi_direct_global_address_symbol(
    const MinicCoreFunction *, const MinicCoreInstruction *, const char **);
bool minic_multi_direct_function_address_symbol(
    const MinicCoreFunction *, const MinicCoreInstruction *, const char **);
/* Core verifies the taken label's CFG identity and pointer result once.
 * Each target still owns local PC-relative ISA emission. */
bool minic_multi_direct_block_address_shape(
    const MinicCoreFunction *, const MinicCoreInstruction *, MinicCoreBlockId *);
bool minic_multi_direct_object_address_location(
    const MinicCoreFunction *, const MinicCoreInstruction *,
    const MinicLP64FramePlan *, size_t *);
bool minic_multi_direct_field_address_shape(
    const MinicCoreFunction *, const MinicCoreInstruction *,
    MinicCoreValueId *, size_t *);
bool minic_multi_direct_dynamic_stack_alloc_shape(
    const MinicCoreFunction *, const MinicCoreInstruction *, bool);
#endif
