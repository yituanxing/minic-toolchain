#include <limits.h>
#include <stdio.h>
int core_m9_checked_int(int, int, int *);
long core_m9_checked_long(long, long, long *);
unsigned int core_m9_checked_uint(unsigned int, unsigned int, unsigned int *);
unsigned long core_m9_checked_ulong(unsigned long, unsigned long, unsigned long *);
unsigned long core_m9_size_sub(unsigned long, unsigned long);
int main(void) {
    int si = 0;
    long sl = 0;
    unsigned int ui = 0;
    unsigned long ul = 0;
    int oi = core_m9_checked_int(INT_MIN, 1, &si);
    int ol = core_m9_checked_long(LONG_MAX, -1L, &sl);
    int oui = core_m9_checked_uint(0U, 1U, &ui);
    int oul = core_m9_checked_ulong(0UL, 1UL, &ul);
    printf("%d %d %d %u %d %ld %d %lu %lu %lu %lu\n",
           oi,
           si,
           oui,
           ui,
           ol,
           sl,
           oul,
           ul,
           core_m9_size_sub(~0UL, 1UL),
           core_m9_size_sub(1UL, ~0UL),
           core_m9_size_sub(42UL, 2UL));
    return 0;
}
