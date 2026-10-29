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
bool minic_core_integer_bitscan_shape(
    const MinicC0Program *, const MinicTargetInfo *,
    const MinicCoreFunction *, const MinicCoreInstruction *,
    unsigned int *width);
#endif
