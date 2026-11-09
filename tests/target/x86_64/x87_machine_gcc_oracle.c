/* Linked by GCC against actual Core -> typed x86 MachineIR functions.
 * Every declaration exercises native SysV extended80 parameter/return ABI. */
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>

extern long double mini_x87_1(long double, long double);
extern long double mini_x87_2(long double, long double);
extern long double mini_x87_3(long double, long double);
extern long double mini_x87_4(long double, long double);
extern long double mini_x87_5(long double);
extern int mini_x87_6(long double, long double);
extern int mini_x87_7(long double, long double);
extern int mini_x87_8(long double, long double);
extern long double mini_x87_9(float);
extern long double mini_x87_10(double);
extern float mini_x87_11(long double);
extern double mini_x87_12(long double);
extern long double mini_x87_13(int32_t);
extern long double mini_x87_14(uint32_t);
extern long double mini_x87_15(int64_t);
extern long double mini_x87_16(uint64_t);
extern int32_t mini_x87_17(long double);
extern uint32_t mini_x87_18(long double);
extern int64_t mini_x87_19(long double);
extern uint64_t mini_x87_20(long double);
extern long double mini_x87_abi_forward(long double);
extern long double mini_x87_varcall(long double);
extern long double mini_x87_literal(void);
extern long double mini_x87_memory(long double *);

long double gnu_x87_identity(long double value) { return value + 1.25L; }

long double gnu_x87_vararg(int sentinel, ...) {
    va_list arguments;
    long double value;
    if (sentinel != 42) return -1.0L;
    va_start(arguments, sentinel);
    value = va_arg(arguments, long double);
    va_end(arguments);
    return value + 1.25L;
}

#define CHECK(predicate, label) do { \
    if (!(predicate)) { fprintf(stderr, "X86_X87_GCC=FAIL %s\n", label); \
        return 1; } \
} while (0)

int main(void) {
    const long double nan = __builtin_nanl("");
    CHECK(mini_x87_1(1.25L, 2.5L) == 3.75L, "add");
    CHECK(mini_x87_2(1.25L, 2.5L) == -1.25L, "subtract");
    CHECK(mini_x87_3(1.25L, 2.5L) == 3.125L, "multiply");
    CHECK(mini_x87_4(1.25L, 2.5L) == 0.5L, "divide");
    CHECK(mini_x87_5(1.25L) == -1.25L, "negate");
    CHECK(mini_x87_6(1.25L, 1.25L) == 1, "equal");
    CHECK(mini_x87_6(nan, 1.25L) == 0, "equal unordered");
    CHECK(mini_x87_7(1.25L, 2.5L) == 1, "less");
    CHECK(mini_x87_7(nan, 2.5L) == 0, "less unordered");
    CHECK(mini_x87_8(2.5L, 2.5L) == 1, "less_equal");
    CHECK(mini_x87_8(2.5L, nan) == 0, "less_equal unordered");
    CHECK(mini_x87_9(3.5f) == 3.5L, "from float32");
    CHECK(mini_x87_10(3.25) == 3.25L, "from float64");
    CHECK(mini_x87_11(3.25L) == 3.25f, "to float32");
    CHECK(mini_x87_12(3.25L) == 3.25, "to float64");
    CHECK(mini_x87_13(-123) == -123.0L, "from signed32");
    CHECK(mini_x87_14(UINT32_C(4000000000)) == 4000000000.0L,
        "from unsigned32");
    CHECK(mini_x87_15(INT64_C(-9223372036854775807)) ==
        -9223372036854775807.0L, "from signed64");
    CHECK(mini_x87_16(UINT64_MAX) == 18446744073709551615.0L,
        "from unsigned64 high bit");
    CHECK(mini_x87_16(UINT64_C(9223372036854775807)) ==
        9223372036854775807.0L, "from unsigned64 low bit");
    CHECK(mini_x87_17(-123.75L) == -123, "to signed32");
    CHECK(mini_x87_18(4000000000.75L) == UINT32_C(4000000000),
        "to unsigned32");
    CHECK(mini_x87_19(-9223372036854775807.0L) ==
        INT64_C(-9223372036854775807), "to signed64");
    CHECK(mini_x87_20(18446744073709551615.0L) == UINT64_MAX,
        "to unsigned64 maximum");
    CHECK(mini_x87_20(9223372036854775813.0L) ==
        UINT64_C(9223372036854775813), "to unsigned64 boundary");
    CHECK(mini_x87_20(42.75L) == 42U, "to unsigned64 low bit");
    CHECK(mini_x87_abi_forward(2.5L) == 3.75L,
        "SysV direct call extended80 stack/return");
    CHECK(mini_x87_varcall(2.5L) == 3.75L,
        "SysV variadic call extended80 overflow stack/return");
    CHECK(mini_x87_literal() == 0x1.0000000000000002p0L,
        "binary128 payload x87 literal 64-bit significand tail");
    {
        long double value = 0x1.0000000000000002p0L;
        CHECK(mini_x87_memory(&value) == value &&
            value == 0x1.0000000000000002p0L,
            "volatile x87 load/store memory roundtrip");
    }
    puts("X86_X87_GCC=PASS numeric=20/20 nan_ordered=3 "
         "u64_boundaries=4 SysV_stack_in_return=1 "
         "direct_call=1 variadic_call=1 literal_tail=1 memory=1");
    return 0;
}
