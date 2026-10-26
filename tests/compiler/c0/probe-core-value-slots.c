#include "target/common/core_value_slots.h"
#include "frontend/type.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* x86 SysV: cumulative rbp-negative end offsets.
 * AAPCS64/LP64D: stable 8B SSA prefix and only wide 16B tail allocations.
 * Neither policy has any dependency on an ISA encoder or physical ABI. */
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

    if (!minic_core_value_slots_build(
            &fn, MINIC_CORE_VALUE_SLOT_LINEAR, 0U, 100U,
            &slots, &end) || slots == NULL || end != 48U ||
        slots[0] != 8U || slots[1] != 24U ||
        slots[2] != 32U || slots[3] != 48U)
        return 1;
    free(slots); slots = NULL;

    if (!minic_core_value_slots_build(
            &fn, MINIC_CORE_VALUE_SLOT_WIDE_TAIL, 64U, 128U,
            &slots, &end) || slots == NULL || end != 96U ||
        slots[0] != SIZE_MAX || slots[1] != 64U ||
        slots[2] != SIZE_MAX || slots[3] != 80U)
        return 2;
    free(slots); slots = NULL;

    /* Failed allocation is transactional: the caller's state does not
     * change, including at limits and when passed malformed Core metadata. */
    end = 123U;
    if (minic_core_value_slots_build(
            &fn, MINIC_CORE_VALUE_SLOT_LINEAR, 0U, 40U,
            &slots, &end) || slots != NULL || end != 123U)
        return 3;
    if (minic_core_value_slots_build(
            &fn, MINIC_CORE_VALUE_SLOT_WIDE_TAIL, 64U, 95U,
            &slots, &end) || slots != NULL || end != 123U)
        return 4;
    if (minic_core_value_slots_build(
            &fn, MINIC_CORE_VALUE_SLOT_WIDE_TAIL, SIZE_MAX-8U, SIZE_MAX,
            &slots, &end) || slots != NULL || end != 123U)
        return 5;
    if (minic_core_value_slots_build(
            &fn, MINIC_CORE_VALUE_SLOT_LINEAR, 101U, 100U,
            &slots, &end) || slots != NULL || end != 123U)
        return 6;
    if (minic_core_value_slots_build(
            &fn, (MinicCoreValueSlotPolicy)99, 0U, 100U,
            &slots, &end) || slots != NULL || end != 123U)
        return 7;
    fn.values = NULL;
    if (minic_core_value_slots_build(
            &fn, MINIC_CORE_VALUE_SLOT_LINEAR, 0U, 100U,
            &slots, &end) || slots != NULL || end != 123U)
        return 8;
    fn.values = values;
    fn.value_count = 0U;
    if (!minic_core_value_slots_build(
            &fn, MINIC_CORE_VALUE_SLOT_WIDE_TAIL, 32U, 32U,
            &slots, &end) || slots != NULL || end != 32U)
        return 9;

    puts("MINIC_FIVE_TARGET_CORE_VALUE_SLOTS=PASS linear wide-tail bounds rollback");
    return 0;
}
