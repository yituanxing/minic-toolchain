/* Native arm64 Linux LL/SC source forms admitted by the target contract.
 * These are full atomic sequences, not standalone instruction examples. */
typedef unsigned long long u64;
typedef unsigned int u32;
typedef unsigned __int128 u128;

u64 minic_llsc_dec_positive(u64 *addr) {
    u64 value, status;
    asm volatile(
        "// atomic64_dec_if_positive\n"
        " prfm pstl1strm, %2\n"
        "1: ldxr %0, %2\n"
        " subs %0, %0, #1\n"
        " b.lt 2f\n"
        " stlxr %w1, %0, %2\n"
        " cbnz %w1, 1b\n"
        " dmb ish\n"
        "2:\n"
        : "=&r"(value), "=&r"(status), "+Q"(*addr)
        :
        : "memory", "cc");
    return value;
}

u64 minic_llsc_cmpxchg128(u128 *addr,
    u64 expected_lo, u64 expected_hi, u64 desired_lo, u64 desired_hi) {
    u64 old_lo, old_hi;
    u32 status;
    asm volatile(
        "//__cmpxchg128\n"
        " prfm pstl1strm, %[v]\n"
        "1: ldxp %[rl], %[rh], %[v]\n"
        " cmp %[rl], %[ol]\n"
        " ccmp %[rh], %[oh], 0, eq\n"
        " b.ne 2f\n"
        " stxp %w[tmp], %[nl], %[nh], %[v]\n"
        " cbnz %w[tmp], 1b\n"
        "2:\n"
        : [v] "+Q"(*addr), [rl] "=&r"(old_lo),
          [rh] "=&r"(old_hi), [tmp] "=&r"(status)
        : [ol] "r"(expected_lo), [oh] "r"(expected_hi),
          [nl] "r"(desired_lo), [nh] "r"(desired_hi)
        : "cc");
    return old_lo;
}
