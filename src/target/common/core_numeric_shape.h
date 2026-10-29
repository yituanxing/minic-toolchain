#ifndef MINIC_TARGET_COMMON_CORE_NUMERIC_SHAPE_H
#define MINIC_TARGET_COMMON_CORE_NUMERIC_SHAPE_H
#include "core/core_ir.h"
#include "frontend/ast.h"
#include "target/target_info.h"
#include <stdbool.h>
#include <stddef.h>
/* ISA-independent numeric operand/result shape, NOT instruction legalization. */
bool minic_core_integer_overflow_shape(
    const MinicC0Program *, const MinicTargetInfo *,
    const MinicCoreFunction *, const MinicCoreInstruction *,
    size_t *width, bool *is_signed);
/* GNU arbitrary-precision add: unsigned 64-bit + signed 32-bit, stored into
 * signed 32-bit. Physical backends must retain the full 128-bit carry/sign
 * before checking the narrowed result; 32-bit flags alone are insufficient. */
bool minic_core_integer_overflow_mixed_add_i32_shape(
    const MinicC0Program *, const MinicTargetInfo *,
    const MinicCoreFunction *, const MinicCoreInstruction *,
    bool *signed_left);
/* GNU add computes the exact unsigned-65-bit mathematical value for
 * u64 + u32 before storing to an i32 result. UDF's checked allocation
 * length uses this shape, distinct from mixed signed i32/u64 addition. */
bool minic_core_integer_overflow_u64_u32_to_i32_shape(
    const MinicC0Program *, const MinicTargetInfo *,
    const MinicCoreFunction *, const MinicCoreInstruction *,
    bool *wide_left);
/* GNU __builtin_mul_overflow computes the full mathematical product, not
 * the usual C arithmetic-conversion result. Mixed 32-bit operand/result
 * signedness needs a 64-bit product and explicit result-range comparison. */
bool minic_core_integer_overflow_mixed_multiply_i32_shape(
    const MinicC0Program *, const MinicTargetInfo *,
    const MinicCoreFunction *, const MinicCoreInstruction *,
    bool *signed_left, bool *signed_right, bool *signed_result);
bool minic_core_integer_bitscan_shape(
    const MinicC0Program *, const MinicTargetInfo *,
    const MinicCoreFunction *, const MinicCoreInstruction *,
    unsigned int *width);
#endif
