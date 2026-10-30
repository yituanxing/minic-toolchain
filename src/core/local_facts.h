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

/* Bounded bit-mask fact for direct integer locals: ORs keep one-bits as an
 * over-approximation; unknown writes, escape, and CFG barriers invalidate. */
bool minic_core_local_fact_possible_one_bits(
    const MinicCoreLowerContext *context, MinicLocalId local_id,
    uint64_t *bits);

bool minic_core_local_fact_or_constant(
    MinicCoreLowerContext *context, MinicLocalId local_id,
    const MinicConstValue *rhs);

/* Conservative interval for nonnegative integer locals. The source
 * expression has already executed; only its possible result range is kept.
 * Unknown assignments, CFG reentry, address escape and volatile drop it. */
bool minic_core_local_fact_nonnegative_range_get(
    const MinicCoreLowerContext *context, MinicLocalId local_id,
    uint64_t *minimum, uint64_t *maximum);
bool minic_core_local_fact_nonnegative_range_set(
    MinicCoreLowerContext *context, MinicLocalId local_id,
    uint64_t minimum, uint64_t maximum);

void minic_core_local_null_pointer_set(MinicCoreLowerContext *context,
                                        MinicLocalId local_id);

bool minic_core_local_null_pointer_get(const MinicCoreLowerContext *context,
                                        MinicLocalId local_id);

#endif
