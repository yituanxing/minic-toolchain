/* An 8-byte record consumes two RV32 linear va_list slots, not one. */
typedef __builtin_va_list MiniVaList;

typedef struct {
    unsigned int low;
    unsigned int high;
} Pair32;

typedef struct {
    unsigned long long value;
} WideAlign;

static unsigned int check_pair(unsigned int tag, ...) {
    MiniVaList ap;
    Pair32 pair;
    unsigned int tail;
    __builtin_va_start(ap, tag);
    pair = __builtin_va_arg(ap, Pair32);
    tail = __builtin_va_arg(ap, unsigned int);
    __builtin_va_end(ap);
    return pair.low == 0x12345678U && pair.high == 0x9abcdef0U &&
           tail == 0x2468ace0U;
}

static unsigned int check_aligned(unsigned int tag, ...) {
    MiniVaList ap;
    WideAlign wide;
    unsigned int tail;
    __builtin_va_start(ap, tag);
    wide = __builtin_va_arg(ap, WideAlign);
    tail = __builtin_va_arg(ap, unsigned int);
    __builtin_va_end(ap);
    return wide.value == 0x123456789abcdef0ULL && tail == 0x13579bdfU;
}

int main(void) {
    Pair32 pair;
    WideAlign wide;
    pair.low = 0x12345678U;
    pair.high = 0x9abcdef0U;
    wide.value = 0x123456789abcdef0ULL;
    if (!check_pair(1U, pair, 0x2468ace0U))
        return 1;
    if (!check_aligned(2U, wide, 0x13579bdfU))
        return 2;
    return 0;
}
