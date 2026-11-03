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

/* Source reentry is not a single yes/no property. IF constant pruning must
 * preserve case/default and every label-address reference; dead-tail pruning
 * checks only jumps from outside the discarded subtree and separately checks
 * already materialized Core label blocks. Switch/loop query one named label.
 * Keep these policies explicit instead of reusing the strongest IF gate. */
typedef enum MinicCoreReentryCheck {
    MINIC_CORE_REENTRY_CHECK_EXTERNAL_JUMP = 1U << 0,
    MINIC_CORE_REENTRY_CHECK_LABEL_ADDRESS = 1U << 1,
    MINIC_CORE_REENTRY_CHECK_CASE_DISPATCH = 1U << 2
} MinicCoreReentryCheck;

typedef enum MinicCoreReentryReason {
    MINIC_CORE_REENTRY_REASON_DIRECT_GOTO = 1U << 0,
    MINIC_CORE_REENTRY_REASON_ASM_GOTO = 1U << 1,
    MINIC_CORE_REENTRY_REASON_LABEL_ADDRESS = 1U << 2,
    MINIC_CORE_REENTRY_REASON_CASE_DISPATCH = 1U << 3,
    MINIC_CORE_REENTRY_REASON_INDETERMINATE = 1U << 4
} MinicCoreReentryReason;

typedef enum MinicCoreReentryPurpose {
    MINIC_CORE_REENTRY_PURPOSE_IF_DISCARDED,
    MINIC_CORE_REENTRY_PURPOSE_SWITCH_LABEL,
    MINIC_CORE_REENTRY_PURPOSE_LOOP_LABEL,
    MINIC_CORE_REENTRY_PURPOSE_UNREACHABLE_TAIL
} MinicCoreReentryPurpose;

typedef struct MinicCoreReentryQuery {
    /* Exactly one selector: a discarded subtree membership array or a single
     * label id. Source edges entirely inside membership are not external. */
    const bool *membership;
    MinicStatementId label_id;
    unsigned int checks; /* bitset of MinicCoreReentryCheck */
    MinicCoreReentryPurpose purpose;
} MinicCoreReentryQuery;

/* The same typed query works with a validated shared incoming-edge index and
 * with its conservative standalone/OOM fallback. A nonzero reason rejects
 * pruning; INDETERMINATE specifically forbids negative reachability proofs.
 * Core dynamically bound statement_blocks are intentionally NOT represented
 * by this immutable AST index and must be checked separately by the caller. */
unsigned int minic_core_reentry_query(
    MinicCoreReentryIndex *index, const MinicC0Program *program,
    const MinicCoreReentryQuery *query);
#endif
