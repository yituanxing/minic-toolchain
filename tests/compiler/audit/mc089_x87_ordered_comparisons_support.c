#include <stdio.h>
#include <stdlib.h>

extern int mc089_less(long double, long double);
extern int mc089_less_equal(long double, long double);
extern int mc089_greater(long double, long double);
extern int mc089_greater_equal(long double, long double);
extern int mc089_equal(long double, long double);
extern int mc089_not_equal(long double, long double);
extern int mc089_direct_greater(void);

typedef int (*Mc089Compare)(long double, long double);

int main(void) {
    static const Mc089Compare compare[] = {
        mc089_less, mc089_less_equal, mc089_greater,
        mc089_greater_equal, mc089_equal, mc089_not_equal
    };
    long double nan_value = strtold("nan", NULL);
    long double positive_inf = strtold("inf", NULL);
    long double negative_inf = -positive_inf;
    long double pair[12][2] = {
        {2.0L, 1.0L}, {1.0L, 2.0L}, {1.0L, 1.0L},
        {-2.0L, -1.0L}, {-1.0L, -2.0L}, {-0.0L, 0.0L},
        {0.0L, -0.0L}, {positive_inf, 2.0L},
        {negative_inf, -3.0L}, {nan_value, 1.0L},
        {1.0L, nan_value}, {nan_value, nan_value}
    };
    size_t i, op;
    int mismatches = mc089_direct_greater() != 1;

    for (i = 0U; i < 12U; ++i) {
        long double a = pair[i][0];
        long double b = pair[i][1];
        int expected[] = {a < b, a <= b, a > b, a >= b, a == b, a != b};
        for (op = 0U; op < 6U; ++op) {
            int got = compare[op](a, b);
            if (got != expected[op]) {
                (void)fprintf(stderr,
                              "MC089_DIFF case=%zu op=%zu got=%d expected=%d\n",
                              i, op, got, expected[op]);
                ++mismatches;
            }
        }
    }
    (void)printf("MC089_MISMATCHES=%d\n", mismatches);
    return mismatches == 0 ? 0 : 1;
}
