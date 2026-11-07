/* Real Linux arch_try_cmpxchg128 contract: ZF, 16-byte memory, RDX:RAX,
 * RBX:RCX, and the .smp_locks PREL32 patch site are inseparable. */
typedef unsigned __int128 u128;
typedef unsigned long u64;
typedef _Bool bool;
union halves { u128 full; struct { u64 low, high; }; };

static bool cmpxchg128(volatile u128 *ptr, u128 *oldp, u128 desired) {
    union halves old_pair = { .full = *oldp };
    union halves new_pair = { .full = desired };
    bool swapped;
    asm volatile(".pushsection .smp_locks,\"a\"\n"
                 ".balign 4\n"
                 ".long 671f - .\n"
                 ".popsection\n"
                 "671:\n\tlock; cmpxchg16b %[ptr]"
                 "\n\t/* output condition code e*/\n"
                 : "=@cce"(swapped), [ptr] "+m"(*ptr),
                   "+a"(old_pair.low), "+d"(old_pair.high)
                 : "b"(new_pair.low), "c"(new_pair.high)
                 : "memory");
    if (!swapped) *oldp = old_pair.full;
    return swapped;
}

int main(void) {
    volatile u128 cell __attribute__((aligned(16))) = (u128)7UL;
    u128 expected = (u128)7UL;
    u128 desired = ((u128)0x123456789abcdef0UL << 64) | (u128)11UL;
    if (!cmpxchg128(&cell, &expected, desired) || cell != desired ||
        expected != (u128)7UL)
        return 1;
    expected = (u128)3UL;
    if (cmpxchg128(&cell, &expected, (u128)9UL) ||
        cell != desired || expected != desired)
        return 2;
    return 42;
}
