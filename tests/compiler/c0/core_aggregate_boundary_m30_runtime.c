#include <stdio.h>

struct CoreM30Pair {
    long a;
    long b;
};

struct CoreM30One {
    long a;
};

extern struct CoreM30Pair core_m30_pair_add(struct CoreM30Pair, struct CoreM30Pair);
extern struct CoreM30Pair core_m30_return_param(struct CoreM30Pair);
extern long core_m30_after_record(struct CoreM30Pair, long);
extern long core_m30_scalar_record_scalar(long, struct CoreM30Pair, long);
extern long core_m30_one_chunk(struct CoreM30One, long);
extern void core_m30_add_ns(struct CoreM30Pair *, unsigned long);

int main(void) {
    static const struct CoreM30Pair left[] = {
        {0L, 0L},
        {1L, 2L},
        {-7L, 13L},
        {123456789L, -987654321L},
    };
    static const struct CoreM30Pair right[] = {
        {3L, 4L},
        {-5L, 8L},
        {1000L, -2000L},
        {-111111111L, 222222222L},
    };
    static const long tails[] = {5L, -9L, 77L, 12345L};
    static const unsigned long ns_values[] = {1UL, 250000000UL, 900000000UL, 1500000000UL};
    unsigned int i;

    for (i = 0U; i < 4U; ++i) {
        struct CoreM30Pair sum = core_m30_pair_add(left[i], right[i]);
        struct CoreM30Pair roundtrip = core_m30_return_param(left[i]);
        struct CoreM30One one = {left[i].a};
        struct CoreM30Pair time_value = {100L + (long)i, 900000000L - (long)i * 100000000L};

        printf("A %u %ld %ld\n", i, sum.a, sum.b);
        printf("R %u %ld %ld\n", i, roundtrip.a, roundtrip.b);
        printf("P %u %ld %ld %ld\n",
               i,
               core_m30_after_record(left[i], tails[i]),
               core_m30_scalar_record_scalar(tails[i], left[i], -tails[i]),
               core_m30_one_chunk(one, tails[i]));
        core_m30_add_ns(&time_value, ns_values[i]);
        printf("N %u %ld %ld\n", i, time_value.a, time_value.b);
    }
    return 0;
}
