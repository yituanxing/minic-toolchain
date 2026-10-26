#include "target/common/core_object_storage.h"
#include <stdio.h>
#include <string.h>

/* Deliberately stub only DataLayout's type query so this native -Werror
 * probe checks actual Common CoreObjectStorage ownership and error paths
 * without involving a platform's ABI, machine code or target runtime. */
static size_t type_queries;
bool minic_data_layout_type(const MinicDataLayout *layout,
                            const MinicC0Program *program, MinicType type,
                            size_t *size, size_t *alignment) {
    (void)type;
    if (layout == NULL || program == NULL || size == NULL || alignment == NULL)
        return false;
    ++type_queries;
    *size = 4U;
    *alignment = 4U;
    return true;
}
static int check(bool ok, int code) { return ok ? 0 : code; }
int main(void) {
    MinicDataLayout layout = {0};
    MinicC0Program program = {0};
    MinicCoreObject object = {0};
    MinicCoreFunction function = {0};
    MinicCoreObjectStoragePlan owned = {0};
    MinicCoreObjectStoragePlan borrowed_plan = {0};
    MinicCoreObjectStoragePlan invalid = {0};
    MinicCoreObjectStoragePlan mismatched = {0};
    bool borrowed = true;
    int err;
    object.element_count = 2U;
    object.explicit_alignment = 16U;
    function.objects = &object;
    function.object_count = 1U;

    if ((err = check(minic_core_object_storage_plan_acquire(
            &program, &layout, &function, NULL, &owned, &borrowed), 1)) ||
        (err = check(!borrowed && owned.count == 1U &&
                     owned.objects != NULL && owned.objects[0].extent == 8U &&
                     owned.objects[0].alignment == 16U && type_queries == 1U, 2)))
        return err;
    if ((err = check(minic_core_object_storage_plan_acquire(
            &program, &layout, &function, &owned,
            &borrowed_plan, &borrowed), 3)) ||
        (err = check(borrowed && borrowed_plan.objects == owned.objects &&
                     borrowed_plan.count == 1U && type_queries == 1U, 4)))
        return err;
    /* A second caller cannot acquire a nonempty destination; borrowed
     * objects are never freed by releasing their view. */
    if (minic_core_object_storage_plan_acquire(
            &program, &layout, &function, &owned, &borrowed_plan, &borrowed))
        return 5;
    minic_core_object_storage_plan_release(&borrowed_plan, true);
    if (borrowed_plan.objects != NULL || borrowed_plan.count != 0U ||
        owned.objects == NULL || owned.objects[0].alignment != 16U)
        return 6;

    mismatched = owned;
    mismatched.count = 2U;
    borrowed = true;
    if (minic_core_object_storage_plan_acquire(
            &program, &layout, &function, &mismatched, &invalid, &borrowed) ||
        borrowed || invalid.objects != NULL || invalid.count != 0U)
        return 7;
    mismatched.count = 1U;
    mismatched.objects = NULL;
    if (minic_core_object_storage_plan_acquire(
            &program, &layout, &function, &mismatched, &invalid, &borrowed))
        return 8;
    object.explicit_alignment = 3U;
    if (minic_core_object_storage_plan_acquire(
            &program, &layout, &function, NULL, &invalid, &borrowed) ||
        borrowed || invalid.objects != NULL || invalid.count != 0U)
        return 9;
    minic_core_object_storage_plan_release(&owned, false);
    if (owned.objects != NULL || owned.count != 0U)
        return 10;
    (void)puts("MINIC_FIVE_TARGET_OBJECT_STORAGE_OWNERSHIP=PASS");
    return 0;
}
