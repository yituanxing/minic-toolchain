/* Counted source ASM for the AArch64/LoongArch64 Common Direct LP64
 * preflight.  Preprocess with -DMINIC_ASM_CASE=255/256/257/512. */
#ifndef MINIC_ASM_CASE
#define MINIC_ASM_CASE 257
#endif

#define NOP1 __asm__ __volatile__("nop")
#define NOP2 NOP1; NOP1
#define NOP4 NOP2; NOP2
#define NOP8 NOP4; NOP4
#define NOP16 NOP8; NOP8
#define NOP32 NOP16; NOP16
#define NOP64 NOP32; NOP32
#define NOP128 NOP64; NOP64
#define NOP256 NOP128; NOP128

int direct_lp64_asm_capacity(void) {
#if MINIC_ASM_CASE == 255
    NOP128; NOP64; NOP32; NOP16; NOP8; NOP4; NOP2; NOP1;
#elif MINIC_ASM_CASE == 256
    NOP256;
#elif MINIC_ASM_CASE == 257
    NOP256; NOP1;
#elif MINIC_ASM_CASE == 512
    NOP256; NOP256;
#else
#error "unsupported test case"
#endif
    return 42;
}
