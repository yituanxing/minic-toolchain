#include <stdio.h>

int core_m7_not_int(int value);
unsigned int core_m7_not_uint(unsigned int value);
long core_m7_not_long(long value);
unsigned long core_m7_not_ulong(unsigned long value);
unsigned long core_m7_size_max(void);

int main(void) {
    printf("%d %u %ld %lu %lu\n",
           core_m7_not_int(0x12345678),
           core_m7_not_uint(0x89abcdefU),
           core_m7_not_long(0x123456789L),
           core_m7_not_ulong(0x123456789abcdef0UL),
           core_m7_size_max());
    return 0;
}
