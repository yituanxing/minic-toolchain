#ifndef MINIC_TARGET_COMMON_CORE_SPILL_LAYOUT_H
#define MINIC_TARGET_COMMON_CORE_SPILL_LAYOUT_H
#include "core/core_ir.h"
#include "frontend/ast.h"
#include "target/data_layout.h"
#include <stdbool.h>
#include <stddef.h>
/* One actual Core SSA allocation per entire function, including its peak.
 * RV32 ILP32 and RV64 LP64 differ only by physical unit bytes (4/8) and
 * ABI-owned stack base.  Caller supplies SIZE_MAX-initialized per-value
 * offsets; Common owns/reuses all temporary block-liveness scratch data. */
bool minic_core_function_reused_spill_layout(
    const MinicC0Program *, const MinicCoreFunction *,
    const MinicDataLayout *, size_t unit_bytes, size_t value_base_offset,
    size_t *value_offsets, size_t *peak_bytes, size_t *failed_block);
#endif
