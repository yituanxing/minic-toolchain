/* Link with the real MachineIR assembly emitted by
 * x86-machine-core-select --emit-fp. This is an executable GCC differential,
 * not a comparison of printer text. Undefined FP-to-integer inputs are
 * deliberately excluded, while unordered comparisons include payload NaNs. */
#include <inttypes.h>
#include <fenv.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern double mir_fp_0(int64_t);
extern double mir_fp_1(uint64_t);
extern int64_t mir_fp_2(double);
extern uint64_t mir_fp_3(double);
extern double mir_fp_4(float);
extern float mir_fp_5(double);
extern double mir_fp_6(double);
extern int32_t mir_fp_7(double, double);
extern int32_t mir_fp_8(double, double);
extern int32_t mir_fp_9(double, double);
extern double mir_fp_10(uint8_t);
extern uint8_t mir_fp_11(double);

__attribute__((noinline)) static double gcc_s64_double(int64_t x) {
    return (double)x;
}
__attribute__((noinline)) static double gcc_u64_double(uint64_t x) {
    return (double)x;
}
__attribute__((noinline)) static int64_t gcc_double_s64(double x) {
    return (int64_t)x;
}
__attribute__((noinline)) static uint64_t gcc_double_u64(double x) {
    return (uint64_t)x;
}
__attribute__((noinline)) static double gcc_float_double(float x) {
    return (double)x;
}
__attribute__((noinline)) static float gcc_double_float(double x) {
    return (float)x;
}
__attribute__((noinline)) static double gcc_double_negate(double x) {
    return -x;
}
__attribute__((noinline)) static int gcc_double_equal(double x, double y) {
    return x == y;
}
__attribute__((noinline)) static int gcc_double_less(double x, double y) {
    return x < y;
}
__attribute__((noinline)) static int gcc_double_less_equal(double x, double y) {
    return x <= y;
}
__attribute__((noinline)) static double gcc_u8_double(uint8_t x) {
    return (double)x;
}
__attribute__((noinline)) static uint8_t gcc_double_u8(double x) {
    return (uint8_t)x;
}

static uint64_t double_bits(double x) {
    uint64_t bits;
    memcpy(&bits, &x, sizeof(bits));
    return bits;
}

static uint32_t float_bits(float x) {
    uint32_t bits;
    memcpy(&bits, &x, sizeof(bits));
    return bits;
}

int main(void) {
    static const int64_t signed_inputs[] = {
        INT64_MIN, -9007199254740993LL, -1, 0, 1,
        9007199254740993LL, INT64_MAX
    };
    static const uint64_t unsigned_inputs[] = {
        0U, 1U, UINT64_C(0x7fffffffffffffff),
        UINT64_C(0x8000000000000000),
        UINT64_C(0x8000000000000400), UINT64_MAX
    };
    static const double signed_conversions[] = {
        -0x1p62, -65535.875, -1.75, -0.0, 0.0, 1.75,
        65535.875, 0x1p62
    };
    static const double unsigned_conversions[] = {
        0.0, 1.0, 4294967296.0, 0x1p63 - 1024.0,
        0x1p63, 0x1p63 + 2048.0, 0x1p64 - 2048.0
    };
    static const float float_inputs[] = {
        0.0f, -0.0f, 1.5f, -1.25f, 0x1p-149f, 0x1.fffffep127f
    };
    static const double double_inputs[] = {
        0.0, -0.0, 1.875, -1.25, 0x1p-149,
        0x1p-1074, 0x1p128, INFINITY, -INFINITY
    };
    static const double compares[] = {
        NAN, INFINITY, -INFINITY, -1.0, -0.0, 0.0, 1.0, 3.5
    };
    static const uint8_t uchar_inputs[] = {0U, 1U, 127U, 128U, 254U, 255U};
    static const double uchar_conversions[] = {
        0.0, 1.0, 127.875, 128.0, 254.875, 255.0
    };
    size_t checked = 0U, i, j;
    for (i = 0U; i < sizeof(signed_inputs)/sizeof(signed_inputs[0]); ++i) {
        const int64_t value = signed_inputs[i];
        if (double_bits(mir_fp_0(value)) !=
            double_bits(gcc_s64_double(value))) goto fail;
        ++checked;
    }
    for (i = 0U; i < sizeof(unsigned_inputs)/sizeof(unsigned_inputs[0]); ++i) {
        const uint64_t value = unsigned_inputs[i];
        if (double_bits(mir_fp_1(value)) !=
            double_bits(gcc_u64_double(value))) goto fail;
        ++checked;
    }
    for (i = 0U; i < sizeof(signed_conversions)/sizeof(signed_conversions[0]); ++i) {
        const double value = signed_conversions[i];
        if (mir_fp_2(value) != gcc_double_s64(value)) goto fail;
        ++checked;
    }
    for (i = 0U; i < sizeof(unsigned_conversions)/sizeof(unsigned_conversions[0]); ++i) {
        const double value = unsigned_conversions[i];
        if (mir_fp_3(value) != gcc_double_u64(value)) goto fail;
        ++checked;
    }
    for (i = 0U; i < sizeof(float_inputs)/sizeof(float_inputs[0]); ++i) {
        const float value = float_inputs[i];
        if (double_bits(mir_fp_4(value)) !=
            double_bits(gcc_float_double(value))) goto fail;
        ++checked;
    }
    for (i = 0U; i < sizeof(double_inputs)/sizeof(double_inputs[0]); ++i) {
        const double value = double_inputs[i];
        if (float_bits(mir_fp_5(value)) !=
            float_bits(gcc_double_float(value))) goto fail;
        if (double_bits(mir_fp_6(value)) !=
            double_bits(gcc_double_negate(value))) goto fail;
        checked += 2U;
    }
    for (i = 0U; i < sizeof(uchar_inputs)/sizeof(uchar_inputs[0]); ++i) {
        const uint8_t value = uchar_inputs[i];
        if (double_bits(mir_fp_10(value)) !=
            double_bits(gcc_u8_double(value))) goto fail;
        ++checked;
    }
    for (i = 0U; i < sizeof(uchar_conversions)/sizeof(uchar_conversions[0]); ++i) {
        const double value = uchar_conversions[i];
        if (mir_fp_11(value) != gcc_double_u8(value)) goto fail;
        ++checked;
    }
    for (i = 0U; i < sizeof(compares)/sizeof(compares[0]); ++i) {
        for (j = 0U; j < sizeof(compares)/sizeof(compares[0]); ++j) {
            const double a = compares[i], b = compares[j];
            if (mir_fp_7(a, b) != gcc_double_equal(a, b) ||
                mir_fp_8(a, b) != gcc_double_less(a, b) ||
                mir_fp_9(a, b) != gcc_double_less_equal(a, b)) goto fail;
            checked += 3U;
        }
    }
    /* The retired native backend deliberately used UCOMISD, not COMISD:
     * quiet NaNs make all three ordered comparisons false without raising
     * FE_INVALID. Valid high-half u64 conversions must not spuriously raise
     * FE_INVALID either. Signaling-NaN and out-of-range casts are excluded. */
    {
        volatile double quiet_nan = NAN;
        volatile double high_unsigned = 0x1p63 + 2048.0;
        if (feclearexcept(FE_ALL_EXCEPT) != 0 ||
            mir_fp_7(quiet_nan, 0.0) != 0 ||
            mir_fp_8(quiet_nan, 0.0) != 0 ||
            mir_fp_9(quiet_nan, 0.0) != 0 ||
            fetestexcept(FE_INVALID) != 0) goto fail;
        if (feclearexcept(FE_ALL_EXCEPT) != 0 ||
            mir_fp_3(high_unsigned) != gcc_double_u64(high_unsigned) ||
            fetestexcept(FE_INVALID) != 0) goto fail;
        checked += 4U;
    }
    printf("X86_FP_GCC_DIFF=PASS checked=%zu u64_boundary=1 nan_ordered=1 fp_invalid=0\n",
        checked);
    return 0;
fail:
    fprintf(stderr, "X86_FP_GCC_DIFF=FAIL after=%zu cases\n", checked);
    return 1;
}
