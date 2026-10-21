#ifndef MINIC_CORE_REENTRY_INDEX_H
#define MINIC_CORE_REENTRY_INDEX_H

#include "frontend/ast.h"
#include <stdbool.h>

/* Optional compilation-scope read-only AST incoming-edge index. Builds
 * fail open to the proven legacy source scans, never claim a missing edge
 * after allocation/validation failure. begin/end bracket Core Set builds.
 *
 * This index currently uses one scoped process-global owner; it is NOT a
 * thread-local or reentrant multi-compilation cache. */
void minic_core_lower_reentry_index_begin(const MinicC0Program *program);
void minic_core_lower_reentry_index_end(void);
bool minic_core_reentry_index_ready(const MinicC0Program *program);

/* Requires a ready index. Given membership[statement_count], mark *unsafe
 * on any incoming external jump, address-taken label or case/default.
 * Incoming internal edges are discarded with their member subgraph. */
void minic_core_reentry_index_check_discarded(
    const MinicC0Program *program, const bool *membership, bool *unsafe);

/* Preserve the exact indexed label reference check and PERF query count.
 * Returns false when the label has an explicit goto/asm-goto/computed-goto
 * reference or when the index is not ready. */
bool minic_core_reentry_index_unreferenced_loop_label(
    const MinicC0Program *program, MinicStatementId label_id);

#endif
