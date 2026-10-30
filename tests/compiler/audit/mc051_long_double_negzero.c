/* MC-051: this TU must be compiled by MiniC; the support TU by GNU cc. */
extern double get_test_zero(void);
extern double get_test_negative_zero(void);
extern double get_test_positive(void);
extern double get_test_negative(void);
extern int check_negative_zero(long double);
extern int check_positive_zero(long double);
extern int check_exact_value(long double, double);
extern int check_negative_zero_float(float);
extern int check_negative_zero_double(double);
extern int report_mask(int);

static int f(void) {
    long double plus_zero = (long double)get_test_zero();
    long double minus_zero = (long double)get_test_negative_zero();
    long double plus_number = (long double)get_test_positive();
    long double minus_number = (long double)get_test_negative();
    int failure = 0;
    if (!check_positive_zero(plus_zero)) failure |= 1;
    if (!check_negative_zero(minus_zero)) failure |= 2;
    if (!check_exact_value(plus_number, 2.5)) failure |= 4;
    if (!check_exact_value(minus_number, -2.5)) failure |= 8;
    if (!check_negative_zero(-plus_zero)) failure |= 16;
    if (!check_positive_zero(-minus_zero)) failure |= 32;
    if (!check_positive_zero(-(-plus_zero))) failure |= 64;
    if (!check_exact_value(-plus_number, -2.5)) failure |= 128;
    if (!check_exact_value(-minus_number, 2.5)) failure |= 256;
    if (!check_negative_zero_float(-(float)get_test_zero())) failure |= 512;
    if (!check_negative_zero_double(-get_test_zero())) failure |= 1024;
    return failure;
}
int main(void) { return report_mask(f()); }
