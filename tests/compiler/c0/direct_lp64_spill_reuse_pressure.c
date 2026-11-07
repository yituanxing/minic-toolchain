/* Shared Core last-use SSA spill allocator must drive both direct LP64
 * targets, including a zero-value function, CFG joins and address-backed
 * scalar locals. This deliberately creates >256 Core SSA results so the
 * historical value_id*8 frame exceeds LoongArch's signed-12-bit offset. */
#define SPILL_STEP(mask, gain) \
    do { if ((input & (mask)) != 0U) sum += (gain); } while (0)

void lp64_empty_values(void) { }

int lp64_spill_pressure(unsigned int input) {
    int sum = 0;
    SPILL_STEP(1U, 1); SPILL_STEP(2U, 2);
    SPILL_STEP(4U, 3); SPILL_STEP(8U, 4);
    SPILL_STEP(16U, 5); SPILL_STEP(32U, 6);
    SPILL_STEP(64U, 7); SPILL_STEP(128U, 8);
    SPILL_STEP(256U, 9); SPILL_STEP(512U, 10);
    SPILL_STEP(1024U, 11); SPILL_STEP(2048U, 12);
    SPILL_STEP(4096U, 13); SPILL_STEP(8192U, 14);
    SPILL_STEP(16384U, 15); SPILL_STEP(32768U, 16);
    SPILL_STEP(1U, 1); SPILL_STEP(2U, 2);
    SPILL_STEP(4U, 3); SPILL_STEP(8U, 4);
    SPILL_STEP(16U, 5); SPILL_STEP(32U, 6);
    SPILL_STEP(64U, 7); SPILL_STEP(128U, 8);
    SPILL_STEP(256U, 9); SPILL_STEP(512U, 10);
    SPILL_STEP(1024U, 11); SPILL_STEP(2048U, 12);
    SPILL_STEP(4096U, 13); SPILL_STEP(8192U, 14);
    SPILL_STEP(16384U, 15); SPILL_STEP(32768U, 16);
    SPILL_STEP(1U, 1); SPILL_STEP(2U, 2);
    SPILL_STEP(4U, 3); SPILL_STEP(8U, 4);
    SPILL_STEP(16U, 5); SPILL_STEP(32U, 6);
    SPILL_STEP(64U, 7); SPILL_STEP(128U, 8);
    SPILL_STEP(256U, 9); SPILL_STEP(512U, 10);
    SPILL_STEP(1024U, 11); SPILL_STEP(2048U, 12);
    SPILL_STEP(4096U, 13); SPILL_STEP(8192U, 14);
    SPILL_STEP(16384U, 15); SPILL_STEP(32768U, 16);
    if (input == 0U) goto EXIT;
    if ((input & 0x80000000U) != 0U) return -1;
EXIT:
    return sum;
}

int main(void) {
    volatile unsigned int runtime_input = 3U;
    lp64_empty_values();
    return lp64_spill_pressure(runtime_input) == 9 &&
           lp64_spill_pressure(0U) == 0 &&
           lp64_spill_pressure(65535U) == 408 ? 42 : 1;
}
