/* GNU-built support: cross-TU baseline and explicit signed-zero observation. */
#include <stdio.h>
double get_test_zero(void) { volatile double v = 0.0; return v; }
double get_test_negative_zero(void) { volatile double v = -0.0; return v; }
double get_test_positive(void) { volatile double v = 2.5; return v; }
double get_test_negative(void) { volatile double v = -2.5; return v; }
int check_negative_zero(long double v) {
    return v == 0.0L && __builtin_signbitl(v) != 0;
}
int check_positive_zero(long double v) {
    return v == 0.0L && __builtin_signbitl(v) == 0;
}
int check_exact_value(long double v, double e) { return v == (long double)e; }
int check_negative_zero_float(float v) {
    return v == 0.0f && __builtin_signbit(v) != 0;
}
int check_negative_zero_double(double v) {
    return v == 0.0 && __builtin_signbit(v) != 0;
}
int report_mask(int failures) {
    printf("failure_mask=0x%x\n", failures);
    return failures != 0;
}
