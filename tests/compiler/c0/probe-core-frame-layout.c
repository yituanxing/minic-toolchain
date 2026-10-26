#include "target/common/core_frame_layout.h"
#include <stdint.h>
#include <stdio.h>

/* Byte geometry is ISA/ABI-neutral; real frame direction/physical offsets
 * are checked by the x86/RV32/RV64/ARM64/LA64 runtime matrices separately. */
int main(void) {
    size_t value = 91U, offset = 92U;
    if (!minic_core_frame_align_up(16U, 16U, &value) || value != 16U ||
        !minic_core_frame_align_up(17U, 16U, &value) || value != 32U ||
        !minic_core_frame_align_up(17U, 6U, &value) || value != 18U ||
        minic_core_frame_align_up(SIZE_MAX, 16U, &value) ||
        minic_core_frame_align_up(17U, 0U, &value) ||
        minic_core_frame_align_up(17U, 8U, NULL))
        return 1;
    value = 3U;
    if (!minic_core_frame_reserve(&value, 8U, 7U, 64U, &offset) ||
        offset != 8U || value != 15U)
        return 2;
    if (!minic_core_frame_reserve(&value, 8U, 0U, 64U, &offset) ||
        offset != 16U || value != 16U)
        return 3;
    if (minic_core_frame_reserve(&value, 16U, 33U, 48U, &offset) ||
        value != 16U || offset != 16U)
        return 4;
    value = SIZE_MAX - 1U;
    offset = 7U;
    if (minic_core_frame_reserve(&value, 16U, 0U, SIZE_MAX, &offset) ||
        value != SIZE_MAX - 1U || offset != 7U)
        return 5;
    value = 0U;
    offset = 8U;
    if (minic_core_frame_reserve(&value, 1U, 1025U, 1024U, &offset) ||
        value != 0U || offset != 8U)
        return 6;
    if (!minic_core_frame_reserve(&value, 1U, 1024U, 1024U, &offset) ||
        value != 1024U || offset != 0U)
        return 7;
    puts("MINIC_FIVE_TARGET_FRAME_GEOMETRY=PASS alignment reservation overflow rollback");
    return 0;
}
