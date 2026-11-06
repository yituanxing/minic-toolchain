#include <limits.h>

/* GNU checked arithmetic uses mathematical operands rather than the normal
 * C usual arithmetic conversions. In particular ULONG_MAX+1 must overflow
 * an int even though its low machine word wraps to zero. */
static int wide_first(unsigned long wide, int narrow, int *out) {
    return __builtin_add_overflow(wide, narrow, out);
}
static int signed_first(int narrow, unsigned long wide, int *out) {
    return __builtin_add_overflow(narrow, wide, out);
}
static int multiply_signed_first(int signed_operand,
    unsigned int unsigned_operand, int *out) {
    return __builtin_mul_overflow(signed_operand, unsigned_operand, out);
}
static int multiply_unsigned_first(unsigned int unsigned_operand,
    int signed_operand, int *out) {
    return __builtin_mul_overflow(unsigned_operand, signed_operand, out);
}

int main(void) {
    int result = 0;
    if (!wide_first(ULONG_MAX, 1, &result) || result != 0)
        return 1;
    if (!signed_first(1, ULONG_MAX, &result) || result != 0)
        return 2;
    if (wide_first(0, -1, &result) || result != -1)
        return 3;
    if (signed_first(INT_MIN, 0, &result) || result != INT_MIN)
        return 4;
    if (wide_first((unsigned long)INT_MAX + 1UL, -1, &result) ||
        result != INT_MAX)
        return 5;
    if (!signed_first(0, (unsigned long)INT_MAX + 1UL, &result) ||
        result != INT_MIN)
        return 6;
    if (multiply_signed_first(-2, 3U, &result) || result != -6)
        return 7;
    if (multiply_unsigned_first(3U, -2, &result) || result != -6)
        return 8;
    if (!multiply_signed_first(INT_MIN, 2U, &result) || result != 0)
        return 9;
    if (multiply_unsigned_first(1U, INT_MIN, &result) || result != INT_MIN)
        return 10;
    if (!multiply_unsigned_first(UINT_MAX, -1, &result) || result != 1)
        return 11;
    return 0;
}
