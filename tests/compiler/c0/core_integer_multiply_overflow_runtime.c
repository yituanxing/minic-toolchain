#include <limits.h>
#include <stdio.h>

int core_m6_checked_int(int left, int right, int *result);
long core_m6_checked_long(long left, long right, long *result);
unsigned long core_m6_checked_ulong(unsigned long left, unsigned long right, unsigned long *result);
unsigned long core_m6_size_mul(unsigned long left, unsigned long right);

int main(void) {
    int int_result;
    long long_result;
    unsigned long ulong_result;
    int int_overflow;
    long long_overflow;
    unsigned long ulong_overflow;
    unsigned long size_ok;
    unsigned long size_overflow;

    int_result = 0;
    long_result = 0;
    ulong_result = 0;
    int_overflow = core_m6_checked_int(50000, 50000, &int_result);
    long_overflow = core_m6_checked_long(LONG_MAX, 2, &long_result);
    ulong_overflow = core_m6_checked_ulong(ULONG_MAX, 2UL, &ulong_result);
    size_ok = core_m6_size_mul(7UL, 9UL);
    size_overflow = core_m6_size_mul(ULONG_MAX, 2UL);

    printf("%d %d %ld %ld %lu %lu %lu %lu\n",
           int_overflow,
           int_result,
           long_overflow,
           long_result,
           ulong_overflow,
           ulong_result,
           size_ok,
           size_overflow);
    return 0;
}
