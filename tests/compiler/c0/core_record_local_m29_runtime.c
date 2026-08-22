#include <stdio.h>

extern unsigned long core_m29_union_roundtrip(unsigned long);
extern unsigned int core_m29_union_low(unsigned long);
extern unsigned long core_m29_record_24(unsigned long);
extern unsigned long core_m29_mul_u64_u32_div(unsigned long, unsigned int, unsigned int);

int main(void) {
    static const unsigned long values[] = {
        0UL,
        1UL,
        0x100000002UL,
        0x123456789abcdef0UL,
    };
    static const unsigned int muls[] = {1U, 3U, 17U, 65537U};
    static const unsigned int divisors[] = {1U, 5U, 97U, 65521U};
    unsigned int i;

    for (i = 0U; i < 4U; ++i) {
        unsigned long value = values[i];
        printf("R %u %lu %u %lu\n",
               i,
               core_m29_union_roundtrip(value),
               core_m29_union_low(value),
               core_m29_record_24(value));
        printf("D %u %lu\n",
               i,
               core_m29_mul_u64_u32_div(value, muls[i], divisors[i]));
    }
    return 0;
}
