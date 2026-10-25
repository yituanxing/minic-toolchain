#ifndef MINIC_TARGET_RISCV64_OBJECT_INTERVALS_H
#define MINIC_TARGET_RISCV64_OBJECT_INTERVALS_H
#include "target/common/core_object_intervals.h"
/* Historical RV64 API remains a wrapper around shared Core SSA analysis. */
typedef MinicCoreObjectIntervalAnalysis MinicRiscv64ObjectIntervalAnalysis;
void minic_riscv64_object_intervals_index(
    const MinicCoreFunction *, MinicRiscv64ObjectIntervalAnalysis *);
bool minic_riscv64_object_interval(
    const MinicC0Program *, const MinicCoreFunction *, MinicCoreObjectId,
    const MinicRiscv64ObjectIntervalAnalysis *, CoreScalarObjectInterval *);
void minic_riscv64_object_intervals_destroy(
    MinicRiscv64ObjectIntervalAnalysis *);
#endif
