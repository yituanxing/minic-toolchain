#ifndef MINIC_TARGET_COMMON_CORE_OBJECT_STORAGE_H
#define MINIC_TARGET_COMMON_CORE_OBJECT_STORAGE_H

#include "core/core_ir.h"
#include "frontend/ast.h"
#include "target/data_layout.h"
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

/* Type layout + repeated objects + explicit alignment, checked once. */
bool minic_core_object_storage(const MinicC0Program *,
                               const MinicDataLayout *,
                               const MinicCoreObject *,
                               MinicCoreObjectStorage *);
bool minic_core_object_storage_plan_build(
    const MinicC0Program *, const MinicDataLayout *,
    const MinicCoreFunction *, MinicCoreObjectStoragePlan *);
void minic_core_object_storage_plan_destroy(
    MinicCoreObjectStoragePlan *);

/* A uniform five-target ownership boundary: borrow Common's immutable
 * object layout or build a checked standalone snapshot. Physical frame
 * offsets, ABI alignment and spill lifetimes remain target-owned.
 * Acquire only into a zero-initialized (unowned) plan; always release. */
bool minic_core_object_storage_plan_acquire(
    const MinicC0Program *, const MinicDataLayout *,
    const MinicCoreFunction *, const MinicCoreObjectStoragePlan *shared,
    MinicCoreObjectStoragePlan *result, bool *borrowed);
void minic_core_object_storage_plan_release(
    MinicCoreObjectStoragePlan *, bool borrowed);

#endif
