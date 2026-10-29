#include <stdarg.h>
typedef unsigned __int128 U128;

extern unsigned long loong_va_pair_direct(U128);
extern unsigned long loong_va_pair_indirect(U128);
extern unsigned long loong_va_spill_direct(U128);
extern unsigned long loong_va_spill_indirect(U128);
extern unsigned long loong_named_pair_control(U128);
extern unsigned long loong_named_split_control(U128);

static U128 reference(void) {
    return ((U128)0x123456789abcdef0ULL << 64) |
           (U128)0xfedcba9876543210ULL;
}

unsigned long va_one(unsigned long tag, ...) {
    va_list ap;
    U128 wide;
    unsigned long tail;
    va_start(ap, tag);
    wide = va_arg(ap, U128);
    tail = va_arg(ap, unsigned long);
    va_end(ap);
    return tag == 11UL && wide == reference() && tail == 19UL;
}

unsigned long va_seven(unsigned long a, unsigned long b,
    unsigned long c, unsigned long d, unsigned long e,
    unsigned long f, unsigned long g, ...) {
    va_list ap;
    U128 wide;
    unsigned long tail;
    va_start(ap, g);
    wide = va_arg(ap, U128);
    tail = va_arg(ap, unsigned long);
    va_end(ap);
    return a == 1UL && b == 2UL && c == 3UL && d == 4UL &&
           e == 5UL && f == 6UL && g == 7UL &&
           wide == reference() && tail == 19UL;
}

unsigned long fixed_one(unsigned long tag, U128 wide) {
    return tag == 11UL && wide == reference();
}

unsigned long fixed_seven(unsigned long a, unsigned long b,
    unsigned long c, unsigned long d, unsigned long e,
    unsigned long f, unsigned long g, U128 wide) {
    return a == 1UL && b == 2UL && c == 3UL && d == 4UL &&
           e == 5UL && f == 6UL && g == 7UL && wide == reference();
}

int main(void) {
    U128 wide = reference();
    if (!loong_va_pair_direct(wide)) return 1;
    if (!loong_va_pair_indirect(wide)) return 2;
    if (!loong_va_spill_direct(wide)) return 3;
    if (!loong_va_spill_indirect(wide)) return 4;
    if (!loong_named_pair_control(wide)) return 5;
    if (!loong_named_split_control(wide)) return 6;
    return 42;
}
