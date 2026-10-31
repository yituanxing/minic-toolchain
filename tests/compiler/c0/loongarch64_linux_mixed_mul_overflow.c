/* Actual operand/result types at Linux 6.6.143 drm_mode_vrefresh and
 * fbcon_set_font. GNU overflow arithmetic compares the exact mathematical
 * product to the destination type's range, regardless of C promotions. */
int drm_like(int clock, unsigned int numerator, unsigned int *product) {
    return __builtin_mul_overflow(clock, numerator, product);
}

int fbcon_like(int size, unsigned int charcount, int *product) {
    return __builtin_mul_overflow(size, charcount, product);
}

int drm_like_negated(unsigned int numerator, int clock,
                     unsigned int *product) {
    return __builtin_mul_overflow(numerator, clock, product);
}

#ifdef MINI_MIXED_OVERFLOW_HOST_TEST
#include <limits.h>
#include <stdio.h>

static int check_one(int lhs, unsigned int rhs) {
    unsigned int u = 0;
    int s = 0;
    const long long exact = (long long)lhs * (long long)rhs;
    const int expected_unsigned = exact < 0 || exact > (long long)UINT_MAX;
    const int expected_signed = exact < INT_MIN || exact > INT_MAX;
    const int got_unsigned = drm_like(lhs, rhs, &u);
    const int got_signed = fbcon_like(lhs, rhs, &s);
    return got_unsigned == expected_unsigned &&
           got_signed == expected_signed &&
           u == (unsigned int)exact && s == (int)exact &&
           drm_like_negated(rhs, lhs, &u) == expected_unsigned;
}

int main(void) {
    static const int signed_samples[] = {
        INT_MIN, INT_MIN + 1, -10000, -1, 0, 1, 2, 10000, INT_MAX
    };
    static const unsigned int unsigned_samples[] = {
        0U, 1U, 2U, 32767U, 65536U, (unsigned int)INT_MAX,
        0x80000000U, UINT_MAX
    };
    unsigned int i, j;
    for (i = 0; i < sizeof(signed_samples)/sizeof(signed_samples[0]); ++i)
        for (j = 0; j < sizeof(unsigned_samples)/sizeof(unsigned_samples[0]); ++j)
            if (!check_one(signed_samples[i], unsigned_samples[j])) {
                fprintf(stderr, "mixed overflow mismatch: %d, %u\n",
                        signed_samples[i], unsigned_samples[j]);
                return 1;
            }
    return 0;
}
#endif
