#include "target/common/core_value_slots.h"
#include "frontend/type.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* All targets share Core's last-use scalar slots; only Direct LP64 needs
 * this separate 16-byte wide sidecar, with checked transactional bounds. */
int main(void) {
    MinicCoreFunction fn = {0};
    MinicCoreValue values[4] = {0};
    MinicCoreTypeContext types;
    MinicCoreType shapes[4] = {0};
    size_t *slots = NULL, end = SIZE_MAX;
    size_t i;
    minic_core_type_context_initialize(&types);
    shapes[0].kind = MINIC_CORE_TYPE_INTEGER;
    shapes[0].has_layout = true;
    shapes[0].size = shapes[0].alignment = 4U;
    shapes[0].value.integer.bit_width = 32U;
    shapes[1].kind = MINIC_CORE_TYPE_INTEGER;
    shapes[1].has_layout = true;
    shapes[1].size = shapes[1].alignment = 16U;
    shapes[1].value.integer.bit_width = 128U;
    shapes[2].kind = MINIC_CORE_TYPE_FLOAT;
    shapes[2].has_layout = true;
    shapes[2].size = shapes[2].alignment = 8U;
    shapes[2].value.floating.format = MINIC_CORE_FLOAT_BINARY64;
    shapes[3].kind = MINIC_CORE_TYPE_FLOAT;
    shapes[3].has_layout = true;
    shapes[3].size = shapes[3].alignment = 16U;
    shapes[3].value.floating.format = MINIC_CORE_FLOAT_EXTENDED;
    for (i = 0U; i < 4U; ++i)
        if (!minic_core_type_register(&types, &shapes[i], &values[i].core_type_id))
            return 1;
    fn.values = values;
    fn.value_count = 4U;
    fn.type_context = &types;
    /* Legacy source descriptors are intentionally contradictory: slot policy
     * must use only the resolved Core execution shapes. */
    values[0].type = minic_type_long_double();
    values[1].type = minic_type_int128();
    values[2].type = minic_type_int128();
    values[3].type = minic_type_int();

    if (!minic_core_wide_value_slots_build(
            &fn, 64U, 128U,
            &slots, &end) || slots == NULL || end != 96U ||
        slots[0] != SIZE_MAX || slots[1] != 64U ||
        slots[2] != SIZE_MAX || slots[3] != 80U)
        return 2;
    free(slots); slots = NULL;

    /* Failed allocation is transactional: the caller's state does not
     * change, including at limits and when passed malformed Core metadata. */
    end = 123U;
    if (minic_core_wide_value_slots_build(
            &fn, 64U, 95U,
            &slots, &end) || slots != NULL || end != 123U)
        return 3;
    if (minic_core_wide_value_slots_build(
            &fn, SIZE_MAX-8U, SIZE_MAX,
            &slots, &end) || slots != NULL || end != 123U)
        return 4;
    if (minic_core_wide_value_slots_build(
            &fn, 101U, 100U,
            &slots, &end) || slots != NULL || end != 123U)
        return 5;
    fn.values = NULL;
    if (minic_core_wide_value_slots_build(
            &fn, 0U, 100U,
            &slots, &end) || slots != NULL || end != 123U)
        return 6;
    fn.values = values;
    fn.value_count = 0U;
    if (!minic_core_wide_value_slots_build(
            &fn, 32U, 32U,
            &slots, &end) || slots != NULL || end != 32U)
        return 7;

    fn.value_count = 4U;
    fn.type_context = NULL;
    if (minic_core_wide_value_slots_build(&fn, 32U, 128U, &slots, &end))
        return 8;
    fn.type_context = &types;
    values[2].core_type_id = MINIC_CORE_TYPE_INVALID;
    if (minic_core_wide_value_slots_build(&fn, 32U, 128U, &slots, &end))
        return 9;
    minic_core_type_context_destroy(&types);

    puts("MINIC_CORE_WIDE_VALUE_SLOTS=PASS coretype_only wide-tail bounds rollback");
    return 0;
}
