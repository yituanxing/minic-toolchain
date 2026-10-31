/* SPDX-License-Identifier: GPL-2.0 */
/* Real Linux x86 special_insns.h RDPKRU/WRPKRU byte and GPR contracts.
 * Both are privileged CPU-state instructions: test object shape only, never
 * execute these functions on the host (PKRU availability is CPU dependent). */
typedef unsigned int u32;

u32 linux_rdpkru(void) {
    u32 ecx = 0;
    u32 edx, pkru;
    asm volatile(".byte 0x0f,0x01,0xee\n\t"
                 : "=a" (pkru), "=d" (edx)
                 : "c" (ecx));
    return pkru;
}

void linux_wrpkru(u32 pkru) {
    u32 ecx = 0, edx = 0;
    asm volatile(".byte 0x0f,0x01,0xef\n\t"
                 : : "a" (pkru), "c" (ecx), "d" (edx));
}

/* Additional live EDX output proves it is not accidentally optimized to
 * an EAX-only result, and that the next computation can read both outputs. */
u32 linux_rdpkru_high(void) {
    u32 ecx = 0;
    u32 edx, pkru;
    asm volatile(".byte 0x0f,0x01,0xee\n\t"
                 : "=a" (pkru), "=d" (edx)
                 : "c" (ecx));
    return pkru ^ edx;
}
