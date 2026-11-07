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

/* A uniform five-target ownership boundary: borrow Common's immutable
 * object layout or build a checked standalone snapshot. Physical frame
 * offsets, ABI alignment and spill lifetimes remain target-owned.
 * Acquire only into a zero-initialized (unowned) plan; always release. */
bool minic_core_object_storage_plan_acquire(
    const MinicCoreFunction *, const MinicCoreObjectStoragePlan *shared,
    MinicCoreObjectStoragePlan *result, bool *borrowed);
void minic_core_object_storage_plan_release(
    MinicCoreObjectStoragePlan *, bool borrowed);

#endif
