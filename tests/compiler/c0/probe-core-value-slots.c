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
    size_t *slots = NULL, end = SIZE_MAX;
    fn.values = values;
    fn.value_count = 4U;
    values[0].type = minic_type_int();
    values[1].type = minic_type_int128();
    values[2].type = minic_type_double();
    values[3].type = minic_type_long_double();

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

    puts("MINIC_CORE_WIDE_VALUE_SLOTS=PASS wide-tail bounds rollback");
    return 0;
}
