/* Unmodified GNU operand roles from Linux 6.6
 * arch/loongarch/include/asm/barrier.h __smp_store_mb(), cases 4/8.
 * Unlike cmpxchg.h __xchg_asm, these stores use named registers, an
 * unsigned-long scratch result and NO explicit "memory" clobber. */
typedef unsigned int u32;
typedef unsigned long long u64;

static volatile u32 state32 = 0x12345678U;
static volatile u64 state64 = 0x123456789abcdef0ULL;

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

int main(void) {
    linux_store_mb32(0xa5b6c7d8U);
    if (state32 != 0xa5b6c7d8U) return 1;
    linux_store_mb64(0xffeeddccbbaa9988ULL);
    if (state64 != 0xffeeddccbbaa9988ULL) return 2;
    linux_store_mb32(0x80000001U);
    if (state32 != 0x80000001U) return 3;
    linux_store_mb64(0x8000000000000001ULL);
    if (state64 != 0x8000000000000001ULL) return 4;
    return 42;
}
