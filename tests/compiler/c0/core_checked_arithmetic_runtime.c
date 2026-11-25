#include <limits.h>
#include <stdio.h>

#define DECLARE(op, type, suffix) type checked_##op##_##suffix(type, type, type *);
#define WIDTHS(op) \
    DECLARE(op, int, int) \
    DECLARE(op, long, long) \
    DECLARE(op, unsigned int, uint) \
    DECLARE(op, unsigned long, ulong)
WIDTHS(add)
WIDTHS(sub)
WIDTHS(mul)
unsigned long size_add(unsigned long, unsigned long);
unsigned long size_sub(unsigned long, unsigned long);
unsigned long size_mul(unsigned long, unsigned long);

#define REQUIRE(expr) do { \
    if (!(expr)) { fprintf(stderr, "overflow contract failed: %s\n", #expr); return 1; } \
} while (0)
#define CHECK(op, type, suffix, a, b, overflow, expected) do { \
    type value = 0; \
    type flag = checked_##op##_##suffix(a, b, &value); \
    REQUIRE(flag == (overflow) && value == (type)(expected)); \
} while (0)
#define NORMAL(op, a, b, expected) \
    CHECK(op, int, int, a, b, 0, expected); \
    CHECK(op, long, long, a, b, 0, expected); \
    CHECK(op, unsigned int, uint, a, b, 0, expected); \
    CHECK(op, unsigned long, ulong, a, b, 0, expected)

int main(void) {
    NORMAL(add, 40, 2, 42);
    NORMAL(sub, 42, 2, 40);
    NORMAL(mul, 7, 9, 63);
    CHECK(add, int, int, INT_MAX, 1, 1, INT_MIN);
    CHECK(add, long, long, LONG_MIN, -1L, 1, LONG_MAX);
    CHECK(add, unsigned int, uint, UINT_MAX, 1U, 1, 0);
    CHECK(add, unsigned long, ulong, ULONG_MAX, 1UL, 1, 0);
    CHECK(sub, int, int, INT_MIN, 1, 1, INT_MAX);
    CHECK(sub, long, long, LONG_MAX, -1L, 1, LONG_MIN);
    CHECK(sub, unsigned int, uint, 0U, 1U, 1, UINT_MAX);
    CHECK(sub, unsigned long, ulong, 0UL, 1UL, 1, ULONG_MAX);
    CHECK(mul, int, int, 50000, 50000, 1, -1794967296);
    CHECK(mul, long, long, LONG_MAX, 2L, 1, -2L);
    CHECK(mul, unsigned int, uint, UINT_MAX, 2U, 1, UINT_MAX - 1U);
    CHECK(mul, unsigned long, ulong, ULONG_MAX, 2UL, 1, ULONG_MAX - 1UL);
    REQUIRE(size_add(ULONG_MAX, 1UL) == ULONG_MAX && size_add(40UL, 2UL) == 42UL);
    REQUIRE(size_sub(ULONG_MAX, 1UL) == ULONG_MAX && size_sub(1UL, ULONG_MAX) == ULONG_MAX);
    REQUIRE(size_sub(42UL, 2UL) == 40UL && size_sub(1UL, 2UL) == ULONG_MAX);
    REQUIRE(size_mul(7UL, 9UL) == 63UL && size_mul(ULONG_MAX, 2UL) == 99UL);
    puts("CORE_CHECKED_ARITHMETIC=PASS");
    return 0;
}
