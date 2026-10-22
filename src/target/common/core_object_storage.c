#include "target/common/core_object_storage.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

bool minic_core_object_storage(const MinicC0Program *program,
                               const MinicDataLayout *layout,
                               const MinicCoreObject *object,
                               MinicCoreObjectStorage *result) {
    size_t size, alignment;
    if (program == NULL || layout == NULL || object == NULL || result == NULL ||
        object->element_count == 0U ||
        !minic_data_layout_type(layout, program, object->type, &size, &alignment) ||
        alignment == 0U || (alignment & (alignment - 1U)) != 0U ||
        size > SIZE_MAX / object->element_count)
        return false;
    if (object->explicit_alignment != 0U) {
        if ((object->explicit_alignment & (object->explicit_alignment - 1U)) != 0U)
            return false;
        if (object->explicit_alignment > alignment)
            alignment = object->explicit_alignment;
    }
    result->extent = size * object->element_count;
    result->alignment = alignment;
    return true;
}

bool minic_core_object_storage_plan_build(
    const MinicC0Program *program, const MinicDataLayout *layout,
    const MinicCoreFunction *function, MinicCoreObjectStoragePlan *plan) {
    size_t i;
    if (program == NULL || layout == NULL || function == NULL || plan == NULL ||
        plan->objects != NULL ||
        (function->object_count != 0U && function->objects == NULL) ||
        function->object_count > SIZE_MAX / sizeof(*plan->objects))
        return false;
    plan->count = 0U;
    if (function->object_count == 0U) return true;
    plan->objects = (MinicCoreObjectStorage *)calloc(
        function->object_count, sizeof(*plan->objects));
    if (plan->objects == NULL) return false;
    for (i = 0U; i < function->object_count; ++i)
        if (!minic_core_object_storage(
                program, layout, &function->objects[i], &plan->objects[i])) {
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
