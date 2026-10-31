/* Each form must fail in MiniC's target-owned hardware ASM admission.
 * These are not permitted to borrow a generic parse-only or scalar path. */
static unsigned char bytes[64] __attribute__((aligned(64)));
void reject(void) {
#if BAD_FORM == 1
    asm volatile("vaddps %ymm0,%ymm1,%ymm2"); /* unknown RAID6 opcode */
#elif BAD_FORM == 2
    asm volatile("movdqa %9,%%xmm0" : : "m" (bytes[0]));
#elif BAD_FORM == 3
    asm volatile("movdqa %%xmm0,%0" : "=r" (bytes[0]));
#elif BAD_FORM == 4
    asm volatile("vmovdqa64 %%zmm31,%0" : "=m" (bytes[0]));
#elif BAD_FORM == 5
    asm volatile("syscall %xmm0");
#endif
}
