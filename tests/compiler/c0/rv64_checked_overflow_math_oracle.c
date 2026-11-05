/* Independent GNU-native arithmetic oracle for the RISC-V LP64 typed
 * checked-overflow selector. This validates the exact selected predicates,
 * modulo result and separate stored-value/SSA-boolean semantics over all
 * four 32/64-bit signednesses. It does NOT replace GNU RV64 ELF/QEMU tests. */
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static uint64_t seed = UINT64_C(0x9e3779b97f4a7c15);
static uint64_t random_word(void) {
    seed ^= seed << 13;
    seed ^= seed >> 7;
    seed ^= seed << 17;
    return seed;
}

static bool check_signed32(int32_t a, int32_t b) {
    int32_t actual;
    int64_t wide;
    bool flag;

    wide = (int64_t)a + (int64_t)b;
    flag = __builtin_add_overflow(a, b, &actual);
    if (actual != (int32_t)wide || flag !=
        (wide != (int64_t)(int32_t)wide)) return false;
    wide = (int64_t)a - (int64_t)b;
    flag = __builtin_sub_overflow(a, b, &actual);
    if (actual != (int32_t)wide || flag !=
        (wide != (int64_t)(int32_t)wide)) return false;
    wide = (int64_t)a * (int64_t)b;
    flag = __builtin_mul_overflow(a, b, &actual);
    return actual == (int32_t)wide && flag ==
        (wide != (int64_t)(int32_t)wide);
}

static bool check_unsigned32(uint32_t a, uint32_t b) {
    uint32_t actual;
    uint64_t wide;
    bool flag;

    wide = (uint64_t)a + (uint64_t)b;
    flag = __builtin_add_overflow(a, b, &actual);
    if (actual != (uint32_t)wide || flag != (wide > UINT32_MAX))
        return false;
    flag = __builtin_sub_overflow(a, b, &actual);
    if (actual != a - b || flag != (a < b)) return false;
    wide = (uint64_t)a * (uint64_t)b;
    flag = __builtin_mul_overflow(a, b, &actual);
    return actual == (uint32_t)wide && flag == (wide > UINT32_MAX);
}

static bool check_signed64(int64_t a, int64_t b) {
    const uint64_t lhs = (uint64_t)a, rhs = (uint64_t)b;
    const uint64_t add = lhs + rhs, sub = lhs - rhs;
    const uint64_t low_product =
        (uint64_t)((unsigned __int128)lhs * (unsigned __int128)rhs);
    const __int128 full_product = (__int128)a * (__int128)b;
    int64_t actual;
    bool flag, predicted;

    predicted = ((~(lhs ^ rhs) & (lhs ^ add)) >> 63) != 0U;
    flag = __builtin_add_overflow(a, b, &actual);
    if ((uint64_t)actual != add || flag != predicted) return false;
    predicted = (((lhs ^ rhs) & (lhs ^ sub)) >> 63) != 0U;
    flag = __builtin_sub_overflow(a, b, &actual);
    if ((uint64_t)actual != sub || flag != predicted) return false;
    /* Multiplication first writes the low product, then checks the inverse
     * division when b!=0, with the INT64_MIN/-1 exceptional pair admitted. */
    predicted = b != 0 && ((a == INT64_MIN && b == -1) ||
        ((int64_t)low_product / b != a));
    flag = __builtin_mul_overflow(a, b, &actual);
    return (uint64_t)actual == low_product && flag == predicted &&
        flag == (full_product > INT64_MAX || full_product < INT64_MIN);
}

static bool check_unsigned64(uint64_t a, uint64_t b) {
    const uint64_t add = a + b, sub = a - b;
    const unsigned __int128 full_product =
        (unsigned __int128)a * (unsigned __int128)b;
    const uint64_t low_product = (uint64_t)full_product;
    uint64_t actual;
    bool flag;

    flag = __builtin_add_overflow(a, b, &actual);
    if (actual != add || flag != (add < a)) return false;
    flag = __builtin_sub_overflow(a, b, &actual);
    if (actual != sub || flag != (a < b)) return false;
    flag = __builtin_mul_overflow(a, b, &actual);
    return actual == low_product && flag ==
        (b != 0U && low_product / b != a) &&
        flag == (full_product > UINT64_MAX);
}

int main(void) {
    static const uint64_t edges[] = {
        UINT64_C(0), UINT64_C(1), UINT64_C(2), UINT64_C(3),
        UINT64_C(0x7fffffff), UINT64_C(0x80000000),
        UINT64_C(0xffffffff), UINT64_C(0x100000000),
        UINT64_C(0x7fffffffffffffff), UINT64_C(0x8000000000000000),
        UINT64_C(0xffffffff00000000), UINT64_MAX
    };
    size_t i, j, checked = 0U;
    for (i = 0U; i < sizeof(edges) / sizeof(edges[0]) + 10000U; ++i) {
        const uint64_t a = i < sizeof(edges) / sizeof(edges[0]) ?
            edges[i] : random_word();
        for (j = 0U; j < sizeof(edges) / sizeof(edges[0]) + 12U; ++j) {
            const uint64_t b = j < sizeof(edges) / sizeof(edges[0]) ?
                edges[j] : random_word();
            if (!check_signed32((int32_t)a, (int32_t)b) ||
                !check_unsigned32((uint32_t)a, (uint32_t)b) ||
                !check_signed64((int64_t)a, (int64_t)b) ||
                !check_unsigned64(a, b)) {
                fprintf(stderr, "RV64_CHECKED_MATH=FAIL a=%" PRIx64
                    " b=%" PRIx64 "\n", a, b);
                return 1;
            }
            checked += 4U;
        }
    }
    printf("RV64_CHECKED_MATH=PASS combinations=%zu 32_64=1 signed_unsigned=1 add_sub_mul=1\n",
           checked);
    return 0;
}
