#ifndef MINIC_CORE_REENTRY_INDEX_H
#define MINIC_CORE_REENTRY_INDEX_H

#include "frontend/ast.h"
#include <stdbool.h>

/* Compilation-owned read-only AST incoming-edge index. Missing or
 * incompletely validated indexes must use conservative source scans;
 * no negative edge proof can come from a failed cache build. */
typedef struct MinicCoreReentryIndex MinicCoreReentryIndex;
MinicCoreReentryIndex *minic_core_reentry_index_create(const MinicC0Program *program);
void minic_core_reentry_index_destroy(MinicCoreReentryIndex *index);
bool minic_core_reentry_index_ready(const MinicCoreReentryIndex *index,
                                   const MinicC0Program *program);

/* Requires a ready index. Given membership[statement_count], mark *unsafe
 * on any incoming external jump, address-taken label or case/default.
 * Incoming internal edges are discarded with their member subgraph. */
void minic_core_reentry_index_check_discarded(
    MinicCoreReentryIndex *index, const MinicC0Program *program,
    const bool *membership, bool *unsafe);

/* Preserve the exact indexed label reference check and PERF query count.
 * Returns false when the label has an explicit goto/asm-goto/computed-goto
 * reference or when the index is not ready. */
bool minic_core_reentry_index_unreferenced_loop_label(
    MinicCoreReentryIndex *index, const MinicC0Program *program,
    MinicStatementId label_id);

#endif
