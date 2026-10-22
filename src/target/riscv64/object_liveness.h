#ifndef MINIC_TARGET_RISCV64_OBJECT_LIVENESS_H
#define MINIC_TARGET_RISCV64_OBJECT_LIVENESS_H
#include "core/core_ir.h"
#include "frontend/ast.h"
#include <stdbool.h>
#include <stddef.h>
/* Preserve RV64's validated scalar object lifetime interval analysis and
 * RECORD_LOAD snapshot reuse. Core IDs remain semantic, never stack offsets.
 * This cache is target-owned and not an architecture-neutral frame plan. */
bool minic_riscv64_record_call_result_snapshot_reuse_candidate(
    const MinicCoreFunction *, MinicCoreObjectId, MinicCoreObjectId *);
bool minic_riscv64_scalar_object_reuse_layout(
    const MinicC0Program *, const MinicCoreFunction *, MinicCoreObjectId,
    bool *, size_t *, size_t *);
void minic_riscv64_object_liveness_set_crossblock_enabled(bool);
void minic_riscv64_object_liveness_begin_function(void);
void minic_riscv64_object_liveness_end_function(void);
#endif
