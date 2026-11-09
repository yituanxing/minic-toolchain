/* AVX512 RAID6: 64-byte ZMM load/XOR/non-temporal store plus real K1
 * predicate generation. Distinguishes true AVX512 hardware from pretending
 * that a parsed template already implements SIMD. */
static unsigned char a[64] __attribute__((aligned(64)));
static unsigned char b[64] __attribute__((aligned(64)));
static unsigned char r[64] __attribute__((aligned(64)));

int main(void) {
    int i;
    for (i = 0; i != 64; ++i) {
        a[i] = (unsigned char)(i * 13 + 7);
        b[i] = (unsigned char)(i * 11 + 19);
    }
    asm volatile("vmovdqa64 %0,%%zmm0\n\t"
                 "vmovdqa64 %1,%%zmm1\n\t"
                 "vpcmpgtb %%zmm0,%%zmm1,%%k1\n\t"
                 "vpmovm2b %%k1,%%zmm2\n\t"
                 "vpxorq %%zmm1,%%zmm0,%%zmm0\n\t"
                 "vmovntdq %%zmm0,%2"
                 : : "m" (a[0]), "m" (b[0]), "m" (r[0]));
    asm volatile("sfence" : : : "memory");
    for (i = 0; i != 64; ++i)
        if (r[i] != (unsigned char)(a[i] ^ b[i])) return 1;
    return 0;
}
