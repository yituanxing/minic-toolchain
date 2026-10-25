#ifndef MINIC_TARGET_COMMON_CORE_OBJECT_INTERVALS_H
#define MINIC_TARGET_COMMON_CORE_OBJECT_INTERVALS_H
#include "core/core_ir.h"
#include "frontend/ast.h"
#include "target/data_layout.h"
#include <stdbool.h>
#include <stddef.h>
/* RV64-private immutable interval facts, never stored in semantic Core. */
typedef struct CoreScalarObjectInterval {
    bool reusable, can_share_across_blocks;
    size_t block_index, first_position, last_position;
} CoreScalarObjectInterval;
typedef struct CoreObjectAddressUse {
    size_t total, direct, block, first, last;
    bool cross_block;
} CoreObjectAddressUse;
typedef struct MinicCoreObjectIntervalAnalysis {
    CoreObjectAddressUse *address_use_index;
    CoreScalarObjectInterval *interval_index;
} MinicCoreObjectIntervalAnalysis;
void minic_core_object_intervals_index(
    const MinicCoreFunction *, MinicCoreObjectIntervalAnalysis *);
bool minic_core_object_interval(
    const MinicC0Program *, const MinicCoreFunction *, MinicCoreObjectId,
    const MinicDataLayout *, size_t unit_bytes,
    const MinicCoreObjectIntervalAnalysis *, CoreScalarObjectInterval *);
void minic_core_object_intervals_destroy(
    MinicCoreObjectIntervalAnalysis *);
#endif
