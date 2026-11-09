/* GNU-built support: cross-TU baseline and explicit signed-zero observation. */
#ifndef MC051_FREESTANDING
#include <stdio.h>
#endif
double get_test_zero(void) { volatile double v = 0.0; return v; }
double get_test_negative_zero(void) { volatile double v = -0.0; return v; }
double get_test_positive(void) { volatile double v = 2.5; return v; }
double get_test_negative(void) { volatile double v = -2.5; return v; }
long double get_test_infinity(void) {
    volatile long double v = __builtin_infl();
    return v;
}
long double get_test_nan(void) {
    volatile long double v = __builtin_nanl("0x1234");
    return v;
}
int check_negative_zero(long double v) {
    return v == 0.0L && __builtin_signbitl(v) != 0;
}
int check_positive_zero(long double v) {
    return v == 0.0L && __builtin_signbitl(v) == 0;
}
int check_exact_value(long double v, double e) { return v == (long double)e; }
int check_negative_infinity(long double v) {
    return __builtin_isinf(v) && __builtin_signbitl(v) != 0;
}
int check_positive_infinity(long double v) {
    return __builtin_isinf(v) && __builtin_signbitl(v) == 0;
}
int check_negative_nan(long double v) {
    return __builtin_isnan(v) && __builtin_signbitl(v) != 0;
}
int check_positive_nan(long double v) {
    return __builtin_isnan(v) && __builtin_signbitl(v) == 0;
}
int check_negative_zero_float(float v) {
    return v == 0.0f && __builtin_signbit(v) != 0;
}
int check_negative_zero_double(double v) {
    return v == 0.0 && __builtin_signbit(v) != 0;
}
int report_mask(int failures) {
#ifndef MC051_FREESTANDING
    printf("failure_mask=0x%x\n", failures);
#endif
    return failures != 0;
}
