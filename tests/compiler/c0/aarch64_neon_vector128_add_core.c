/* MiniC source-identity ADD test, distinct from the strict GNU ACLE header
 * differential. It exercises real V-register Core lowering without a fake
 * uint128/record surrogate. Cross-assembly/QEMU is a separate required gate. */
typedef __Uint64x2_t vector128_u64;
typedef __Uint8x16_t vector128_u8;

union lanes64 {
    vector128_u64 vec;
    unsigned long long word[2];
};

union lanes8 {
    vector128_u8 vec;
    unsigned char byte[16];
};

static union lanes64 left64, right64, out64;
static union lanes8 left8, right8, out8;

int main(void) {
    unsigned int i;
    left64.word[0] = 0x123456789abcdef0ULL;
    left64.word[1] = 0xdeadbeef00440088ULL;
    right64.word[0] = 0xff00ff00ff00ff00ULL;
    right64.word[1] = 0x0011223344556677ULL;
    out64.vec = left64.vec + right64.vec;
    if (out64.word[0] != (left64.word[0] + right64.word[0]) ||
        out64.word[1] != (left64.word[1] + right64.word[1])) return 1;

    for (i = 0; i < 16; ++i) {
        left8.byte[i] = (unsigned char)(i * 13U + 5U);
        right8.byte[i] = (unsigned char)(i * 7U + 9U);
    }
    out8.vec = left8.vec + right8.vec;
    for (i = 0; i < 16; ++i)
        if (out8.byte[i] != (unsigned char)(left8.byte[i] + right8.byte[i]))
            return 2;
    return 0;
}
