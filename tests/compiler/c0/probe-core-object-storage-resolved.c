#include "target/common/core_object_storage.h"
#include "target/data_layout.h"
#include <stdint.h>
#include <stdio.h>

/* The resolved five-target Core object path must never reopen C DataLayout.
 * This deliberately conflicting C stub makes accidental fallback visible. */
static unsigned int legacy_layout_queries;
bool minic_data_layout_type(const MinicDataLayout *layout,
                            const MinicC0Program *program, MinicType type,
                            size_t *size, size_t *alignment) {
    (void)type;
    if (layout == NULL || program == NULL || size == NULL || alignment == NULL)
        return false;
    ++legacy_layout_queries;
    *size = 4U;
    *alignment = 4U;
    return true;
}

int main(void) {
    MinicCoreTypeContext types;
    MinicCoreType type = {0};
    MinicCoreTypeId integer_id, incomplete_id;
    MinicCoreObject object = {0};
    MinicCoreFunction fn = {0};
    MinicCoreObjectStoragePlan plan = {0};

    minic_core_type_context_initialize(&types);
    type.kind = MINIC_CORE_TYPE_INTEGER;
    type.has_layout = true;
    type.size = 8U;
    type.alignment = 8U;
    type.value.integer.bit_width = 64U;
    if (!minic_core_type_register(&types, &type, &integer_id)) return 1;
    type = (MinicCoreType){0};
    type.kind = MINIC_CORE_TYPE_OPAQUE;
    if (!minic_core_type_register(&types, &type, &incomplete_id)) return 2;

    object.core_type_id = integer_id;
    object.element_count = 3U;
    object.explicit_alignment = 16U;
    fn.type_context = &types;
    fn.objects = &object;
    fn.object_count = 1U;
    if (!minic_core_object_storage_plan_build(&fn, &plan)) return 3;
    if (plan.count != 1U || plan.objects[0].extent != 24U ||
        plan.objects[0].alignment != 16U || legacy_layout_queries != 0U)
        return 4;
    minic_core_object_storage_plan_destroy(&plan);

    object.core_type_id = MINIC_CORE_TYPE_INVALID;
    if (minic_core_object_storage_plan_build(&fn, &plan) ||
        legacy_layout_queries != 0U || plan.objects != NULL) return 5;
    object.core_type_id = incomplete_id;
    if (minic_core_object_storage_plan_build(&fn, &plan) ||
        legacy_layout_queries != 0U || plan.objects != NULL) return 6;
    object.core_type_id = integer_id;
    object.element_count = SIZE_MAX;
    if (minic_core_object_storage_plan_build(&fn, &plan) ||
        legacy_layout_queries != 0U || plan.objects != NULL) return 7;
    object.element_count = 1U;
    object.explicit_alignment = 3U;
    if (minic_core_object_storage_plan_build(&fn, &plan) ||
        legacy_layout_queries != 0U || plan.objects != NULL) return 8;

    /* Standalone fixtures must also supply CoreType. Even a valid old C
     * descriptor must not reactivate the retired target layout oracle. */
    fn.type_context = NULL;
    object.explicit_alignment = 0U;
    if (minic_core_object_storage_plan_build(&fn, &plan) ||
        legacy_layout_queries != 0U || plan.objects != NULL)
        return 9;
    minic_core_type_context_destroy(&types);
    (void)puts("CORE_STORAGE_RESOLVED_NO_SOURCE_LAYOUT=PASS");
    return 0;
}
