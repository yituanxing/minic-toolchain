#ifndef MINIC_TARGET_RISCV64_SPILL_LAYOUT_H
#define MINIC_TARGET_RISCV64_SPILL_LAYOUT_H
#include "core/core_ir.h"
#include "frontend/ast.h"
#include <stdbool.h>
#include <stddef.h>
/* Target-owned block-local spill layout; result slots cannot alias
 * operands before the instruction consumes them. */
bool minic_riscv64_block_reused_spill_layout(const MinicC0Program *,
    const MinicCoreFunction *, const MinicCoreBlock *, size_t, size_t *, size_t *);
#endif
