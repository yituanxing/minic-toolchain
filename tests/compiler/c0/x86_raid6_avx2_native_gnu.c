/* AVX2 RAID6: load two aligned 32-byte vectors and non-temporally XOR.
 * All work is real YMM hardware ASM; no C SIMD intrinsic/auto-vectorizer. */
static unsigned char a[32] __attribute__((aligned(32)));
static unsigned char b[32] __attribute__((aligned(32)));
static unsigned char r[32] __attribute__((aligned(32)));

int main(void) {
    int i;
    for (i = 0; i != 32; ++i) {
        a[i] = (unsigned char)(i * 17 + 9);
        b[i] = (unsigned char)(i * 7 + 21);
    }
    asm volatile("vmovdqa %0,%%ymm0\n\t"
                 "vmovdqa %1,%%ymm1\n\t"
                 "vpxor %%ymm1,%%ymm0,%%ymm0\n\t"
                 "vmovntdq %%ymm0,%2"
                 : : "m" (a[0]), "m" (b[0]), "m" (r[0]));
    asm volatile("sfence" : : : "memory");
    for (i = 0; i != 32; ++i)
        if (r[i] != (unsigned char)(a[i] ^ b[i])) return 1;
    return 0;
}
