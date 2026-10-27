#include "target/riscv32/rv32_internal.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The genuine ILP32 frame machine helpers, compiled as host C with
 * unreachable unrelated frame-init functions discarded by the linker.
 * No QEMU or synthetic target assembly interpretation is required. */
int main(void) {
    Rv32Frame frame = {0};
    size_t offsets[] = {16U, SIZE_MAX, 60U};
    FILE *out = tmpfile();
    char output[500] = {0};
    size_t n;
    if (out == NULL) return 1;
    frame.value_count = 3U;
    frame.value_offsets = offsets;
    frame.frame_size = 64U;
    if (!rv32_slot(out, &frame, 0U, "t0", true) ||
        !rv32_pair_slot(out, &frame, 0U, "t0", "t1", false) ||
        !rv32_stack(out, "lw", "t6", 4096U))
        return 2;
    if (rv32_slot(out, &frame, 1U, "t0", true) ||
        rv32_pair_slot(out, &frame, 1U, "t0", "t1", true) ||
        rv32_pair_slot(out, &frame, 2U, "t0", "t1", true) ||
        rv32_slot(out, &frame, MINIC_CORE_VALUE_INVALID, "t0", true) ||
        rv32_pair_slot(out, &frame, (MinicCoreValueId)10U, "t0", "t1", true))
        return 3;
    if (rv32_stack(out, "sw", "t6", 4096U) ||
        rv32_stack(out, "sb", "t6", 4096U) ||
        rv32_stack(out, "sh", "t6", 4096U) ||
        rv32_stack(out, "lw", "t0", SIZE_MAX))
        return 4;
    if (rv32_slot(out, NULL, 0U, "t0", true) ||
        rv32_slot(out, &frame, 0U, NULL, true) ||
        rv32_slot(NULL, &frame, 0U, "t0", true) ||
        rv32_incoming_word(out, NULL, 0U, "a0") ||
        rv32_outgoing_word(out, NULL, 0U, "a0") ||
        rv32_incoming_word(out, &frame, 0U, NULL) ||
        rv32_outgoing_word(out, &frame, 0U, NULL))
        return 5;
    if (fflush(out) != 0 || fseek(out, 0L, SEEK_SET) != 0)
        return 6;
    n = fread(output, 1U, sizeof(output) - 1U, out);
    if (fclose(out) != 0 || n == 0U ||
        strstr(output, "  lw t0, 16(sp)\n") == NULL ||
        strstr(output, "  sw t1, 20(sp)\n") == NULL ||
        strstr(output, "  li t6, 4096\n") == NULL ||
        strstr(output, "  lw t6, 0(t6)\n") == NULL)
        return 7;
    puts("MINIC_RV32_FRAME_ACCESS=PASS valid=3 sentinel=2 bound=1 scratch=4 null=7");
    return 0;
}
