#ifndef MINIC_CORE_LOCAL_FACTS_H
#define MINIC_CORE_LOCAL_FACTS_H
#include "core/core_lower_internal.h"
#include <stddef.h>

/* Internal semantic fact-state interface: snapshots are caller-owned;
 * no fact may outlive a write, escaped local or unknown CFG merge.
 * Full-source proofs and fact writes that depend on them remain with
 * AST-to-Core lowering until their CFG oracle has a separate owner. */
bool minic_core_local_fact_index(const MinicCoreLowerContext *context,
                                      MinicLocalId local_id,
                                      size_t *index);

bool minic_core_inline_spec_fact_trace_enabled(const MinicCoreLowerContext *context);

MinicCoreLocalIntegerConstant *minic_core_local_facts_snapshot(
    const MinicCoreLowerContext *context);

void minic_core_local_facts_restore(
    MinicCoreLowerContext *context,
    const MinicCoreLocalIntegerConstant *snapshot);

/* Meet two reachable source-expression CFG paths. Preserve a known value
 * only when both paths agree; carry escape/specialization effects from either
 * path. The caller owns both snapshots and must check allocation success. */
bool minic_core_local_facts_meet(
    MinicCoreLowerContext *context,
    const MinicCoreLocalIntegerConstant *left,
    const MinicCoreLocalIntegerConstant *right);

void minic_core_local_facts_clear_known(MinicCoreLowerContext *context);

void minic_core_local_fact_invalidate(MinicCoreLowerContext *context,
                                           MinicLocalId local_id);

void minic_core_local_fact_escape(MinicCoreLowerContext *context,
                                       MinicLocalId local_id);

bool minic_core_local_fact_get(const MinicCoreLowerContext *context,
                                    MinicLocalId local_id,
                                    MinicConstValue *value);

void minic_core_local_null_pointer_set(MinicCoreLowerContext *context,
                                        MinicLocalId local_id);

bool minic_core_local_null_pointer_get(const MinicCoreLowerContext *context,
                                        MinicLocalId local_id);

#endif
