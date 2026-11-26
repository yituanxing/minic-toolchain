#ifndef MINIC_TARGET_COMMON_CORE_OBJECT_STORAGE_H
#define MINIC_TARGET_COMMON_CORE_OBJECT_STORAGE_H

#include "core/core_ir.h"
#include <stdbool.h>
#include <stddef.h>

/* Semantic byte extent/alignment only: every target still owns frame
 * direction, spill reuse, physical address and ABI register decisions. */
typedef struct MinicCoreObjectStorage {
    size_t extent;
    size_t alignment;
} MinicCoreObjectStorage;

typedef struct MinicCoreObjectStoragePlan {
    size_t count;
    MinicCoreObjectStorage *objects;
} MinicCoreObjectStoragePlan;

/* Core-owned type layout + repeated objects + explicit alignment.  This is
 * the normal compiler path: an invalid or unlaid-out CoreType fails closed,
 * without consulting the C frontend's DataLayout. */
bool minic_core_object_storage_resolved(const MinicCoreTypeContext *,
                                        const MinicCoreObject *,
                                        MinicCoreObjectStorage *);

bool minic_core_object_storage_plan_build(
    const MinicCoreFunction *, MinicCoreObjectStoragePlan *);
void minic_core_object_storage_plan_destroy(
    MinicCoreObjectStoragePlan *);

/* Checked immutable borrow from the one prepared Common FunctionPlan.
 * Frame offsets, ABI register placement and stack directions remain target-owned. */
bool minic_core_object_storage_plan_borrow(
    const MinicCoreFunction *, const MinicCoreObjectStoragePlan *,
    const MinicCoreObjectStoragePlan **borrowed);

#endif
