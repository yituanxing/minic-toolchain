/* GNU/glibc is used only as a test oracle: MiniC's literal parser itself
 * remains ISO C11 and has no host floating-point-library dependency. */
#define __STDC_WANT_IEC_60559_TYPES_EXT__ 1
#include "frontend/binary128_literal.h"

#include <float.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__GLIBC__) && defined(__SIZEOF_FLOAT128__) && \
    defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
__extension__ typedef _Float128 MinicOracleFloat128;

static bool check_one(const char *text) {
    uint64_t low = 0U, high = 0U;
    uint64_t oracle[2] = {0U, 0U};
    MinicOracleFloat128 quad = strtof128(text, NULL);
    (void)memcpy(oracle, &quad, sizeof(oracle));
    if (!minic_binary128_parse_literal(text, strlen(text), 113U, &low, &high) ||
        low != oracle[0] || high != oracle[1]) {
        (void)fprintf(stderr,
                      "binary128 literal mismatch %s: minic=%016" PRIx64
                      ":%016" PRIx64 " glibc=%016" PRIx64 ":%016" PRIx64 "\n",
                      text, high, low, oracle[1], oracle[0]);
        return false;
    }
    if (LDBL_MANT_DIG == 64 && LDBL_MAX_EXP == 16384 &&
        sizeof(long double) >= 10U) {
        unsigned char bytes[sizeof(long double)];
        uint64_t x87_mantissa = 0U;
        uint16_t x87_sign_exp = 0U;
        uint64_t minic_mantissa;
        unsigned int biased;
        long double host = strtold(text, NULL);
        (void)memcpy(bytes, &host, sizeof(bytes));
        (void)memcpy(&x87_mantissa, bytes, sizeof(x87_mantissa));
        (void)memcpy(&x87_sign_exp, bytes + 8U, sizeof(x87_sign_exp));
        if (!minic_binary128_parse_literal(text, strlen(text), 64U, &low, &high))
            return false;
        biased = (unsigned int)((high >> 48U) & 0x7fffU);
        minic_mantissa = (biased == 0U ? 0U : UINT64_C(0x8000000000000000)) |
                         ((high & UINT64_C(0x0000ffffffffffff)) << 15U) |
                         (low >> 49U);
        if (minic_mantissa != x87_mantissa ||
            (uint16_t)(high >> 48U) != x87_sign_exp) {
            (void)fprintf(stderr,
                          "x87 literal mismatch %s: minic=%04x:%016" PRIx64
                          " glibc=%04x:%016" PRIx64 "\n",
                          text, (unsigned int)(uint16_t)(high >> 48U), minic_mantissa,
                          (unsigned int)x87_sign_exp, x87_mantissa);
            return false;
        }
    }
    return true;
}

static uint32_t next_word(uint32_t *state) {
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state;
}

int main(void) {
    static const char *const cases[] = {
        "0", "-0", "+0.0", "1", "2", "0.1", "-0.1",
        "1.0000000000000001", "1.000000000000000000000000000000001",
        "0x1.0000000000000001p0", "0x1.0000000000000002p0",
        "0x1.fffffffffffffffep0", "0x1.ffffffffffffffffffffp0",
        "0x1p-16445", "0x1p-16494", "0x1p-16495", "0x1p16383",
        "0x1.ffffffffffffffffffffffffffffp16383", "1e4932", "1e4933",
        "1e5000", "1e-4950", "1e-4960", "1e-4966", "1e-4967",
        "-2.71828182845904523536028747135266249775724709369995",
        "3.14159265358979323846264338327950288419716939937510",
        "0x.8p0", "0X1.8P+3", "0x1.4p-7",
    };
    size_t index;
    uint32_t seed = UINT32_C(0x41524743);
    for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        if (!check_one(cases[index])) return 1;
    }
    for (index = 0U; index < 256U; ++index) {
        char literal[128];
        char *cursor = literal;
        size_t digit;
        int exponent;
        bool hex = (index & 1U) != 0U;
        if ((next_word(&seed) & 1U) != 0U) *cursor++ = '-';
        if (hex) {
            *cursor++ = '0';
            *cursor++ = 'x';
        }
        *cursor++ = (char)('1' + next_word(&seed) % 9U);
        *cursor++ = '.';
        for (digit = 0U; digit < 45U; ++digit) {
            uint32_t value = next_word(&seed) % (hex ? 16U : 10U);
            *cursor++ = (char)(value < 10U ? '0' + value : 'a' + value - 10U);
        }
        exponent = (int)(next_word(&seed) % (hex ? 33601U : 10401U)) -
                   (hex ? 16800 : 5200);
        (void)snprintf(cursor, sizeof(literal) - (size_t)(cursor - literal),
                       hex ? "p%d" : "e%d", exponent);
        if (!check_one(literal)) return 1;
    }
    (void)puts("BINARY128_LITERAL_ORACLE=PASS x87=64 binary128=113 cases=286");
    return 0;
}
#else
int main(void) {
    (void)puts("BINARY128_LITERAL_ORACLE=SKIP glibc/_Float128/LE host unavailable");
    return 0;
}
#endif
