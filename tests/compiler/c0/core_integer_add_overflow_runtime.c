#include <limits.h>
#include <stdio.h>

int core_m8_checked_int(int left, int right, int *result);
long core_m8_checked_long(long left, long right, long *result);
unsigned int core_m8_checked_uint(unsigned int left, unsigned int right, unsigned int *result);
unsigned long core_m8_checked_ulong(unsigned long left, unsigned long right, unsigned long *result);
unsigned long core_m8_size_add(unsigned long left, unsigned long right);

int main(void) {
    int si = 0;
    long sl = 0;
    unsigned int ui = 0;
    unsigned long ul = 0;
    int oi = core_m8_checked_int(INT_MAX, 1, &si);
    int ol = core_m8_checked_long(LONG_MIN, -1L, &sl);
    int oui = core_m8_checked_uint(UINT_MAX, 1U, &ui);
    int oul = core_m8_checked_ulong(ULONG_MAX, 1UL, &ul);
    printf("%d %d %d %u %d %ld %d %lu %lu %lu\n",
           oi,
           si,
           oui,
           ui,
           ol,
           sl,
           oul,
           ul,
           core_m8_size_add(ULONG_MAX, 1UL),
           core_m8_size_add(40UL, 2UL));
    return 0;
}
