/* GNU LoongArch LP64D: only *unnamed* 16-aligned wide scalars skip an odd
 * GAR. Named __int128 retains the ordinary a7/stack split rule. */
typedef unsigned __int128 U128;
typedef unsigned long (*VaOneFn)(unsigned long, ...);
typedef unsigned long (*VaSevenFn)(unsigned long, unsigned long,
    unsigned long, unsigned long, unsigned long, unsigned long,
    unsigned long, ...);

extern unsigned long va_one(unsigned long tag, ...);
extern unsigned long va_seven(unsigned long a, unsigned long b,
    unsigned long c, unsigned long d, unsigned long e,
    unsigned long f, unsigned long g, ...);
extern unsigned long fixed_one(unsigned long tag, U128 value);
extern unsigned long fixed_seven(unsigned long a, unsigned long b,
    unsigned long c, unsigned long d, unsigned long e,
    unsigned long f, unsigned long g, U128 value);

unsigned long loong_va_pair_direct(U128 value) {
    return va_one(11UL, value, 19UL);
}

unsigned long loong_va_pair_indirect(U128 value) {
    VaOneFn callee = va_one;
    return callee(11UL, value, 19UL);
}

unsigned long loong_va_spill_direct(U128 value) {
    return va_seven(1UL, 2UL, 3UL, 4UL, 5UL, 6UL, 7UL,
                    value, 19UL);
}

unsigned long loong_va_spill_indirect(U128 value) {
    VaSevenFn callee = va_seven;
    return callee(1UL, 2UL, 3UL, 4UL, 5UL, 6UL, 7UL,
                  value, 19UL);
}

unsigned long loong_named_pair_control(U128 value) {
    return fixed_one(11UL, value);
}

unsigned long loong_named_split_control(U128 value) {
    return fixed_seven(1UL, 2UL, 3UL, 4UL, 5UL, 6UL, 7UL, value);
}
