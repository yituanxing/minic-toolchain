/* Unmodified GNU operand roles from Linux 6.6
 * arch/loongarch/include/asm/barrier.h __smp_store_mb(), cases 4/8.
 * Unlike cmpxchg.h __xchg_asm, these stores use named registers, an
 * unsigned-long scratch result and NO explicit "memory" clobber. */
typedef unsigned int u32;
typedef unsigned long long u64;

static volatile u32 state32 = 0x12345678U;
static volatile u64 state64 = 0x123456789abcdef0ULL;
struct guarded_word {
    volatile u32 before;
    volatile u32 value;
    volatile u32 after;
};
static struct guarded_word guarded = {
    0x10203040U, 0x50607080U, 0x90a0b0c0U
};

void linux_store_mb32(u32 value) {
    unsigned long tmp;
    volatile u32 *p = &state32;
    __asm__ __volatile__(
        "amswap_db.w %[tmp], %[val], %[mem]\n"
        : [mem] "+ZB" (*p), [tmp] "=&r" (tmp)
        : [val] "r" (value)
        : );
}

void linux_store_mb64(u64 value) {
    unsigned long tmp;
    volatile u64 *p = &state64;
    __asm__ __volatile__(
        "amswap_db.d %[tmp], %[val], %[mem]\n"
        : [mem] "+ZB" (*p), [tmp] "=&r" (tmp)
        : [val] "r" (value)
        : );
}

/* A `.w` AMDB must affect only the 32-bit C lvalue. The GNU `+ZB`
 * descriptor is not a license to spill the machine scratch through the
 * adjacent words. Both sentinels are volatile to expose byte-width bugs. */
void linux_store_mb_guarded(u32 value) {
    unsigned long tmp;
    volatile u32 *p = &guarded.value;
    __asm__ __volatile__(
        "amswap_db.w %[tmp], %[val], %[mem]\n"
        : [mem] "+ZB" (*p), [tmp] "=&r" (tmp)
        : [val] "r" (value)
        : );
}

int main(void) {
    linux_store_mb32(0xa5b6c7d8U);
    if (state32 != 0xa5b6c7d8U) return 1;
    linux_store_mb64(0xffeeddccbbaa9988ULL);
    if (state64 != 0xffeeddccbbaa9988ULL) return 2;
    linux_store_mb32(0x80000001U);
    if (state32 != 0x80000001U) return 3;
    linux_store_mb64(0x8000000000000001ULL);
    if (state64 != 0x8000000000000001ULL) return 4;
    linux_store_mb_guarded(0xdeadbeefU);
    if (guarded.value != 0xdeadbeefU ||
        guarded.before != 0x10203040U ||
        guarded.after != 0x90a0b0c0U) return 5;

    /* Original GNU memory-role values are reloaded on every iteration.
     * Deterministic wraparound exercises sign bit, zero and many distinct
     * subword patterns against the GNU compiled reference function. */
    u32 step32 = 0x8912ffffU;
    u64 step64 = 0x8123456789abcdefULL;
    for (unsigned int i = 0U; i < 4096U; ++i) {
        step32 = step32 * 1664525U + 1013904223U;
        step64 ^= step64 << 13;
        step64 ^= step64 >> 7;
        step64 ^= step64 << 17;
        linux_store_mb32(step32);
        linux_store_mb64(step64);
        linux_store_mb_guarded(step32 ^ 0x5a5a5a5aU);
        if (state32 != step32 || state64 != step64 ||
            guarded.value != (step32 ^ 0x5a5a5a5aU) ||
            guarded.before != 0x10203040U ||
            guarded.after != 0x90a0b0c0U)
            return 6;
    }
    return 42;
}
