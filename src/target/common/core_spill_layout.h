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
#endif
