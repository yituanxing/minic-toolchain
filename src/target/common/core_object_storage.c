#include "target/common/core_object_storage.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static bool core_object_storage_checked_shape(
    const MinicCoreObject *object, size_t size, size_t alignment,
    MinicCoreObjectStorage *result) {
    if (object == NULL || result == NULL ||
        alignment == 0U || (alignment & (alignment - 1U)) != 0U)
        return false;
    if (object->explicit_alignment != 0U) {
        if ((object->explicit_alignment & (object->explicit_alignment - 1U)) != 0U)
            return false;
        if (object->explicit_alignment > alignment)
            alignment = object->explicit_alignment;
    }
    /* The complete CoreType owns the entire object extent, including arrays.
     * CoreObject must not re-apply a second declarator repetition factor. */
    result->extent = size;
    result->alignment = alignment;
    return true;
}

bool minic_core_object_storage_resolved(const MinicCoreTypeContext *types,
                                        const MinicCoreObject *object,
                                        MinicCoreObjectStorage *result) {
    const MinicCoreType *type;
    if (object == NULL || result == NULL ||
        (type = minic_core_type_get(types, object->core_type_id)) == NULL ||
        !type->has_layout)
        return false;
    return core_object_storage_checked_shape(
        object, type->size, type->alignment, result);
}

bool minic_core_object_storage_plan_build(
    const MinicCoreFunction *function, MinicCoreObjectStoragePlan *plan) {
    size_t i;
    if (function == NULL || function->type_context == NULL || plan == NULL ||
        plan->objects != NULL || plan->count != 0U ||
        (function->object_count != 0U && function->objects == NULL) ||
        function->object_count > SIZE_MAX / sizeof(*plan->objects))
        return false;
    plan->count = 0U;
    if (function->object_count == 0U) return true;
    plan->objects = (MinicCoreObjectStorage *)calloc(
        function->object_count, sizeof(*plan->objects));
    if (plan->objects == NULL) return false;
    for (i = 0U; i < function->object_count; ++i)
        if (!minic_core_object_storage_resolved(
                function->type_context, &function->objects[i], &plan->objects[i])) {
            minic_core_object_storage_plan_destroy(plan);
            return false;
        }
    plan->count = function->object_count;
    return true;
}

void minic_core_object_storage_plan_destroy(MinicCoreObjectStoragePlan *plan) {
    if (plan == NULL) return;
    free(plan->objects);
    (void)memset(plan, 0, sizeof(*plan));
}

/* Shared resource ownership for five machine backends. No target-specific
 * memory addresses, register assignment, spill policy or stack direction. */
bool minic_core_object_storage_plan_acquire(
    const MinicCoreFunction *fn, const MinicCoreObjectStoragePlan *shared,
    MinicCoreObjectStoragePlan *result, bool *borrowed) {
    if (fn == NULL || fn->type_context == NULL || result == NULL || borrowed == NULL ||
        result->objects != NULL || result->count != 0U) return false;
    *borrowed = false;
    if (shared != NULL) {
        if (shared->count != fn->object_count ||
            (shared->count != 0U && shared->objects == NULL)) return false;
        *result = *shared;
        *borrowed = true;
        return true;
    }
    return minic_core_object_storage_plan_build(fn, result);
}

void minic_core_object_storage_plan_release(
    MinicCoreObjectStoragePlan *plan, bool borrowed) {
    if (plan == NULL) return;
    if (borrowed) *plan = (MinicCoreObjectStoragePlan){0};
    else minic_core_object_storage_plan_destroy(plan);
}
