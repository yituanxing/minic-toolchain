/* GNU checked arithmetic: operands keep their own signedness, and the
 * third expression selects an 8/16-bit destination without being evaluated.
 * Includes the real __builtin_add_overflow_p(int,int,(signed char)0) shape. */
int narrow_i8_add(int left, int right) {
    return __builtin_add_overflow_p(left, right, (signed char)0);
}
int narrow_u8_add(unsigned int left, unsigned int right) {
    return __builtin_add_overflow_p(left, right, (unsigned char)0);
}
int narrow_i16_add(int left, unsigned int right) {
    return __builtin_add_overflow_p(left, right, (short)0);
}
int narrow_u16_add(unsigned int left, int right) {
    return __builtin_add_overflow_p(left, right, (unsigned short)0);
}
int narrow_i8_sub(int left, int right) {
    return __builtin_sub_overflow_p(left, right, (signed char)0);
}
int narrow_u8_sub(unsigned int left, unsigned int right) {
    return __builtin_sub_overflow_p(left, right, (unsigned char)0);
}
int narrow_i16_sub(int left, unsigned int right) {
    return __builtin_sub_overflow_p(left, right, (short)0);
}
int narrow_u16_sub(unsigned int left, int right) {
    return __builtin_sub_overflow_p(left, right, (unsigned short)0);
}
int narrow_i8_mul(int left, int right) {
    return __builtin_mul_overflow_p(left, right, (signed char)0);
}
int narrow_u8_mul(unsigned int left, unsigned int right) {
    return __builtin_mul_overflow_p(left, right, (unsigned char)0);
}
int narrow_i16_mul(int left, unsigned int right) {
    return __builtin_mul_overflow_p(left, right, (short)0);
}
int narrow_u16_mul(unsigned int left, int right) {
    return __builtin_mul_overflow_p(left, right, (unsigned short)0);
}

#ifdef MINIC_OVERFLOW_NARROW_INVALID
int invalid_bool_destination(int left, int right) {
    return __builtin_add_overflow_p(left, right, (_Bool)0);
}
#endif

#ifdef MINIC_OVERFLOW_NARROW_NATIVE_TEST
#include <limits.h>
#include <stdio.h>

static int expected(__int128 exact, int result_bits, int result_signed) {
    const __int128 min = result_signed ? -((__int128)1 << (result_bits - 1)) : 0;
    const __int128 max = result_signed ? (((__int128)1 << (result_bits - 1)) - 1)
                                       : (((__int128)1 << result_bits) - 1);
    return exact < min || exact > max;
}

int main(void) {
    static const int signed_values[] = {
        INT_MIN, INT_MIN + 1, -65536, -32769, -129, -1, 0,
        1, 127, 128, 255, 32767, 65535, INT_MAX
    };
    static const unsigned unsigned_values[] = {
        0U, 1U, 127U, 255U, 256U, 32768U, 65535U,
        65536U, 0x80000000U, UINT_MAX
    };
    unsigned i, j;
    for (i = 0; i < sizeof(signed_values)/sizeof(signed_values[0]); ++i) {
        for (j = 0; j < sizeof(unsigned_values)/sizeof(unsigned_values[0]); ++j) {
            const int s = signed_values[i];
            const unsigned u = unsigned_values[j];
            const __int128 sum = (__int128)s + (__int128)u;
            const __int128 diff = (__int128)s - (__int128)u;
            const __int128 reverse_diff = (__int128)u - (__int128)s;
            const __int128 product = (__int128)s * (__int128)u;
            if (narrow_i16_add(s, u) != expected(sum, 16, 1) ||
                narrow_u16_add(u, s) != expected(sum, 16, 0) ||
                narrow_i16_sub(s, u) != expected(diff, 16, 1) ||
                narrow_u16_sub(u, s) != expected(reverse_diff, 16, 0) ||
                narrow_i16_mul(s, u) != expected(product, 16, 1) ||
                narrow_u16_mul(u, s) != expected(product, 16, 0)) {
                fprintf(stderr, "mixed narrow mismatch %d %u\n", s, u);
                return 1;
            }
        }
    }
    for (i = 0; i < sizeof(signed_values)/sizeof(signed_values[0]); ++i)
        for (j = 0; j < sizeof(signed_values)/sizeof(signed_values[0]); ++j) {
            const int lhs = signed_values[i], rhs = signed_values[j];
            if (narrow_i8_add(lhs, rhs) != expected((__int128)lhs + rhs, 8, 1) ||
                narrow_i8_sub(lhs, rhs) != expected((__int128)lhs - rhs, 8, 1) ||
                narrow_i8_mul(lhs, rhs) != expected((__int128)lhs * rhs, 8, 1))
                return 2;
        }
    for (i = 0; i < sizeof(unsigned_values)/sizeof(unsigned_values[0]); ++i)
        for (j = 0; j < sizeof(unsigned_values)/sizeof(unsigned_values[0]); ++j) {
            const unsigned lhs = unsigned_values[i], rhs = unsigned_values[j];
            if (narrow_u8_add(lhs, rhs) != expected((__int128)lhs + rhs, 8, 0) ||
                narrow_u8_sub(lhs, rhs) != expected((__int128)lhs - rhs, 8, 0) ||
                narrow_u8_mul(lhs, rhs) != expected((__int128)lhs * rhs, 8, 0))
                return 3;
        }
    return 0;
}
#endif
