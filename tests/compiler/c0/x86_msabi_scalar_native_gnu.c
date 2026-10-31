#include <stdio.h>

/* GNU GCC owns the MS x64 callees and the SysV oracle. Both direct calls
 * and typed EFI callbacks must agree with GCC's native register/stack ABI. */
typedef long ms_zero_t(void);
typedef long ms_one_t(long);
typedef long ms_four_t(long, long, long, long);
typedef long ms_five_t(long, long, long, long, long);
typedef long ms_seven_t(long, long, long, long, long, long, long);
typedef double ms_mixed_t(double, long, float, long, double, long, double);
struct MSCallbacks {
    ms_zero_t __attribute__((ms_abi)) *zero;
    ms_one_t __attribute__((ms_abi)) *one;
    ms_four_t __attribute__((ms_abi)) *four;
    ms_five_t __attribute__((ms_abi)) *five;
    ms_seven_t __attribute__((ms_abi)) *seven;
    ms_mixed_t __attribute__((ms_abi)) *mixed;
};

__attribute__((ms_abi, noinline)) long ms_scalar_zero(void) { return 101; }
__attribute__((ms_abi, noinline)) long ms_scalar_one(long a) { return a + 13; }
__attribute__((ms_abi, noinline)) long ms_scalar_four(
    long a, long b, long c, long d) {
    return a + 10 * b + 100 * c + 1000 * d;
}
__attribute__((ms_abi, noinline)) long ms_scalar_five(
    long a, long b, long c, long d, long e) {
    return a + 10 * b + 100 * c + 1000 * d + 10000 * e;
}
__attribute__((ms_abi, noinline)) long ms_scalar_seven(
    long a, long b, long c, long d, long e, long f, long g) {
    return a + 10 * b + 100 * c + 1000 * d + 10000 * e +
           100000 * f + 1000000 * g;
}
__attribute__((ms_abi, noinline)) double ms_scalar_mixed(
    double a, long b, float c, long d, double e, long f, double g) {
    return a + 2.0 * (double)b + (double)c + 3.0 * (double)d +
           e + 4.0 * (double)f + g;
}

extern long minic_ms_direct_zero(void);
extern long minic_ms_direct_one(void);
extern long minic_ms_direct_four(void);
extern long minic_ms_direct_five(void);
extern long minic_ms_direct_seven(void);
extern double minic_ms_direct_mixed(void);
extern long minic_ms_indirect_zero(struct MSCallbacks *);
extern long minic_ms_indirect_one(struct MSCallbacks *);
extern long minic_ms_indirect_four(struct MSCallbacks *);
extern long minic_ms_indirect_five(struct MSCallbacks *);
extern long minic_ms_indirect_seven(struct MSCallbacks *);
extern double minic_ms_indirect_mixed(struct MSCallbacks *);

int main(void) {
    struct MSCallbacks cb = {
        ms_scalar_zero, ms_scalar_one, ms_scalar_four,
        ms_scalar_five, ms_scalar_seven, ms_scalar_mixed
    };
    long zero = ms_scalar_zero();
    long one = ms_scalar_one(11);
    long four = ms_scalar_four(1, 2, 3, 4);
    long five = ms_scalar_five(1, 2, 3, 4, 5);
    long seven = ms_scalar_seven(1, 2, 3, 4, 5, 6, 7);
    double mixed = ms_scalar_mixed(2.5, 11, 3.25f, 22, 4.5, 33, 5.5);

    if (minic_ms_direct_zero() != zero || minic_ms_indirect_zero(&cb) != zero ||
        minic_ms_direct_one() != one || minic_ms_indirect_one(&cb) != one ||
        minic_ms_direct_four() != four || minic_ms_indirect_four(&cb) != four ||
        minic_ms_direct_five() != five || minic_ms_indirect_five(&cb) != five ||
        minic_ms_direct_seven() != seven || minic_ms_indirect_seven(&cb) != seven ||
        minic_ms_direct_mixed() != mixed || minic_ms_indirect_mixed(&cb) != mixed) {
        (void)fprintf(stderr,
            "MS_X64_SCALAR=FAIL direct/indirect values expected %ld %ld %ld %ld %ld %.3f\n",
            zero, one, four, five, seven, mixed);
        return 1;
    }
    puts("MS_X64_SCALAR=PASS direct=6 indirect=6 positional_FP=1 stack=3 shadow=32");
    return 0;
}
