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
bool minic_core_integer_bitscan_shape(
    const MinicC0Program *, const MinicTargetInfo *,
    const MinicCoreFunction *, const MinicCoreInstruction *,
    unsigned int *width);
#endif
