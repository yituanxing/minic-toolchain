#ifndef MINIC_TARGET_COMMON_CORE_SPILL_LAYOUT_H
#define MINIC_TARGET_COMMON_CORE_SPILL_LAYOUT_H
#include "core/core_ir.h"
#include "frontend/ast.h"
#include "target/data_layout.h"
#include <stdbool.h>
#include <stddef.h>
/* One Core SSA last-use implementation; ILP32 and LP64 own layout/XLEN. */
bool minic_core_block_reused_spill_layout(const MinicC0Program *,
    const MinicCoreFunction *, const MinicCoreBlock *,
    const MinicDataLayout *, size_t unit_bytes, size_t value_base_offset,
    size_t *value_offsets, size_t *peak_bytes);
/* The entire Core function's maximum block-local SSA spill region and
 * subsequent per-value placement have one owner. These walks are identical
 * for RV32 ILP32 (4-byte units) and RV64 LP64 (8-byte units); the target
 * still chooses base address, stack direction, ABI alignment and frame. */
bool minic_core_function_reused_spill_peak(
    const MinicC0Program *, const MinicCoreFunction *,
    const MinicDataLayout *, size_t unit_bytes, size_t value_base_offset,
    size_t *peak_bytes, size_t *failed_block);
bool minic_core_function_reused_spill_assign(
    const MinicC0Program *, const MinicCoreFunction *,
    const MinicDataLayout *, size_t unit_bytes, size_t value_base_offset,
    size_t maximum_block_bytes, size_t *value_offsets, size_t *failed_block);
#endif
