/* Linux RAID6-style fixed XMM registers. GNU basic asm spells %xmm,
 * extended asm must spell %%xmm; the source memory operand is an address,
 * even though the declared C lvalue is only one byte wide. */
static unsigned char a[32] __attribute__((aligned(16))) = {
    0x15, 0x27, 0x39, 0x4b, 0x5d, 0x6f, 0x71, 0x83,
    0x95, 0xa7, 0xb9, 0xcb, 0xdd, 0xef, 0xf1, 0x03
};
static unsigned char b[32] __attribute__((aligned(16))) = {
    0x24, 0x18, 0x64, 0x50, 0x8c, 0x78, 0xb4, 0xa0,
    0xdc, 0xc8, 0x14, 0x00, 0x3c, 0x28, 0x54, 0x40
};
static unsigned char r[32] __attribute__((aligned(16)));

int main(void) {
    int i;
    asm volatile("pxor %xmm0,%xmm0");
    asm volatile("movdqa %0,%%xmm0" : : "m" (a[0]));
    asm volatile("movdqa %0,%%xmm1" : : "m" (b[0]));
    asm volatile("pxor %xmm1,%xmm0");
    asm volatile("movdqa %%xmm0,%0" : "=m" (r[0]));
    for (i = 0; i != 16; ++i)
        if (r[i] != (unsigned char)(a[i] ^ b[i])) return 1;
    return 0;
}
