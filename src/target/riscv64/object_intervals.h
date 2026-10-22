#ifndef MINIC_TARGET_RISCV64_OBJECT_INTERVALS_H
#define MINIC_TARGET_RISCV64_OBJECT_INTERVALS_H
#include "core/core_ir.h"
#include "frontend/ast.h"
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
typedef struct MinicRiscv64ObjectIntervalAnalysis {
    CoreObjectAddressUse *address_use_index;
    CoreScalarObjectInterval *interval_index;
} MinicRiscv64ObjectIntervalAnalysis;
void minic_riscv64_object_intervals_index(
    const MinicCoreFunction *, MinicRiscv64ObjectIntervalAnalysis *);
bool minic_riscv64_object_interval(
    const MinicC0Program *, const MinicCoreFunction *, MinicCoreObjectId,
    const MinicRiscv64ObjectIntervalAnalysis *, CoreScalarObjectInterval *);
void minic_riscv64_object_intervals_destroy(
    MinicRiscv64ObjectIntervalAnalysis *);
#endif
