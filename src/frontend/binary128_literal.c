#include "frontend/binary128_literal.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Decimal conversion is exact integer arithmetic, not an intermediate
 * host double or long double. The small arbitrary-precision integers below
 * hold the decimal significand and powers of five, and the final division
 * rounds once to the target's significand precision (ties to even). */
typedef struct MinicLiteralBig {
    uint32_t *limbs;
    size_t count;
    size_t capacity;
} MinicLiteralBig;

static void big_dispose(MinicLiteralBig *value) {
    free(value->limbs);
    value->limbs = NULL;
    value->count = 0U;
    value->capacity = 0U;
}

static bool big_reserve(MinicLiteralBig *value, size_t count) {
    uint32_t *replacement;
    size_t capacity;
    if (count <= value->capacity) return true;
    capacity = value->capacity == 0U ? 4U : value->capacity;
    while (capacity < count) {
        if (capacity > SIZE_MAX / 2U) return false;
        capacity *= 2U;
    }
    if (capacity > SIZE_MAX / sizeof(uint32_t)) return false;
    replacement = (uint32_t *)realloc(value->limbs, capacity * sizeof(uint32_t));
    if (replacement == NULL) return false;
    value->limbs = replacement;
    value->capacity = capacity;
    return true;
}

static void big_trim(MinicLiteralBig *value) {
    while (value->count != 0U && value->limbs[value->count - 1U] == 0U)
        --value->count;
}

static bool big_from_u32(MinicLiteralBig *value, uint32_t input) {
    if (input == 0U) {
        value->count = 0U;
        return true;
    }
    if (!big_reserve(value, 1U)) return false;
    value->limbs[0] = input;
    value->count = 1U;
    return true;
}

static bool big_copy(MinicLiteralBig *destination, const MinicLiteralBig *source) {
    if (!big_reserve(destination, source->count)) return false;
    if (source->count != 0U)
        (void)memcpy(destination->limbs, source->limbs,
                     source->count * sizeof(uint32_t));
    destination->count = source->count;
    return true;
}

static bool big_multiply_add(MinicLiteralBig *value, uint32_t factor, uint32_t add) {
    uint64_t carry = add;
    size_t index;
    if (!big_reserve(value, value->count + 1U)) return false;
    for (index = 0U; index < value->count; ++index) {
        uint64_t product = (uint64_t)value->limbs[index] * factor + carry;
        value->limbs[index] = (uint32_t)product;
        carry = product >> 32U;
    }
    if (carry != 0U) value->limbs[value->count++] = (uint32_t)carry;
    return true;
}

static size_t big_bit_length(const MinicLiteralBig *value) {
    uint32_t top;
    size_t bits;
    if (value->count == 0U) return 0U;
    top = value->limbs[value->count - 1U];
    bits = (value->count - 1U) * 32U;
    while (top != 0U) {
        ++bits;
        top >>= 1U;
    }
    return bits;
}

static bool big_shift_left(MinicLiteralBig *value, size_t bits) {
    size_t words = bits / 32U;
    unsigned int rest = (unsigned int)(bits % 32U);
    size_t old_count = value->count;
    size_t index;
    uint32_t high_carry;
    if (old_count == 0U || bits == 0U) return true;
    if (words > SIZE_MAX - old_count - 1U ||
        !big_reserve(value, old_count + words + 1U)) return false;
    high_carry = rest == 0U ? 0U :
        value->limbs[old_count - 1U] >> (32U - rest);
    for (index = old_count; index != 0U; --index) {
        uint32_t shifted = value->limbs[index - 1U] << rest;
        if (rest != 0U && index > 1U)
            shifted |= value->limbs[index - 2U] >> (32U - rest);
        value->limbs[index - 1U + words] = shifted;
    }
    for (index = 0U; index < words; ++index) value->limbs[index] = 0U;
    value->count = old_count + words;
    if (high_carry != 0U) value->limbs[value->count++] = high_carry;
    return true;
}

static void big_shift_right_one(MinicLiteralBig *value) {
    uint32_t carry = 0U;
    size_t index = value->count;
    while (index != 0U) {
        uint32_t part = value->limbs[--index];
        value->limbs[index] = (part >> 1U) | carry;
        carry = (part & 1U) << 31U;
    }
    big_trim(value);
}

static int big_compare(const MinicLiteralBig *a, const MinicLiteralBig *b) {
    size_t index;
    if (a->count != b->count) return a->count > b->count ? 1 : -1;
    index = a->count;
    while (index != 0U) {
        --index;
        if (a->limbs[index] != b->limbs[index])
            return a->limbs[index] > b->limbs[index] ? 1 : -1;
    }
    return 0;
}

static void big_subtract(MinicLiteralBig *a, const MinicLiteralBig *b) {
    uint64_t borrow = 0U;
    size_t index;
    for (index = 0U; index < a->count; ++index) {
        uint64_t right = (index < b->count ? b->limbs[index] : 0U) + borrow;
        uint64_t left = a->limbs[index];
        a->limbs[index] = (uint32_t)(left - right);
        borrow = left < right ? 1U : 0U;
    }
    big_trim(a);
}

static bool big_set_bit(MinicLiteralBig *value, size_t bit) {
    size_t word = bit / 32U;
    if (!big_reserve(value, word + 1U)) return false;
    while (value->count <= word) value->limbs[value->count++] = 0U;
    value->limbs[word] |= UINT32_C(1) << (bit % 32U);
    return true;
}

static void big_clear_bit(MinicLiteralBig *value, size_t bit) {
    size_t word = bit / 32U;
    if (word < value->count) value->limbs[word] &= ~(UINT32_C(1) << (bit % 32U));
    big_trim(value);
}

static uint64_t big_get_u64(const MinicLiteralBig *value, size_t word) {
    uint64_t low = word < value->count ? value->limbs[word] : 0U;
    uint64_t high = word + 1U < value->count ? value->limbs[word + 1U] : 0U;
    return low | (high << 32U);
}

static bool big_compare_scaled(const MinicLiteralBig *left,
                               const MinicLiteralBig *right, int64_t shift,
                               int *result) {
    MinicLiteralBig shifted = {0};
    bool ok;
    if (shift >= 0) {
        ok = big_copy(&shifted, right) &&
             big_shift_left(&shifted, (size_t)shift);
        if (ok) *result = big_compare(left, &shifted);
    } else {
        ok = big_copy(&shifted, left) &&
             big_shift_left(&shifted, (size_t)(-shift));
        if (ok) *result = big_compare(&shifted, right);
    }
    big_dispose(&shifted);
    return ok;
}

static bool big_divide_round(MinicLiteralBig *numerator,
                             const MinicLiteralBig *denominator,
                             MinicLiteralBig *quotient) {
    MinicLiteralBig shifted = {0};
    bool ok = true;
    while (big_compare(numerator, denominator) >= 0) {
        size_t shift = big_bit_length(numerator) - big_bit_length(denominator);
        if (shift > 128U ||
            !big_copy(&shifted, denominator) || !big_shift_left(&shifted, shift)) {
            ok = false;
            break;
        }
        if (big_compare(numerator, &shifted) < 0) {
            big_shift_right_one(&shifted);
            --shift;
        }
        big_subtract(numerator, &shifted);
        if (!big_set_bit(quotient, shift)) {
            ok = false;
            break;
        }
    }
    if (ok && !big_shift_left(numerator, 1U)) ok = false;
    if (ok) {
        int remainder_cmp = big_compare(numerator, denominator);
        if (remainder_cmp > 0 ||
            (remainder_cmp == 0 && quotient->count != 0U &&
             (quotient->limbs[0] & 1U) != 0U))
            ok = big_multiply_add(quotient, 1U, 1U);
    }
    big_dispose(&shifted);
    return ok;
}

static void literal_zero(unsigned int sign, uint64_t *low, uint64_t *high) {
    *low = 0U;
    *high = (uint64_t)sign << 63U;
}

static void literal_inf(unsigned int sign, uint64_t *low, uint64_t *high) {
    *low = 0U;
    *high = ((uint64_t)sign << 63U) | UINT64_C(0x7fff000000000000);
}

static bool literal_round_rational(const MinicLiteralBig *numerator,
                                   const MinicLiteralBig *denominator,
                                   int64_t binary_exp, unsigned int precision,
                                   unsigned int sign, uint64_t *low,
                                   uint64_t *high) {
    const int64_t min_exp = -16382;
    const int64_t max_exp = 16383;
    MinicLiteralBig scaled_num = {0};
    MinicLiteralBig scaled_den = {0};
    MinicLiteralBig quotient = {0};
    int64_t exponent;
    int64_t scale;
    int comparison;
    bool normal;
    bool ok;

    if (numerator->count == 0U) {
        literal_zero(sign, low, high);
        return true;
    }
    exponent = (int64_t)big_bit_length(numerator) -
               (int64_t)big_bit_length(denominator) + binary_exp;
    if (!big_compare_scaled(numerator, denominator, exponent - binary_exp,
                            &comparison)) return false;
    if (comparison < 0) --exponent;
    if (exponent > max_exp) {
        literal_inf(sign, low, high);
        return true;
    }
    if (exponent < min_exp - (int64_t)precision) {
        literal_zero(sign, low, high);
        return true;
    }
    normal = exponent >= min_exp;
    scale = (int64_t)(precision - 1U) - (normal ? exponent : min_exp);
    scale += binary_exp;
    ok = big_copy(&scaled_num, numerator) &&
         big_copy(&scaled_den, denominator);
    if (ok && scale >= 0) ok = big_shift_left(&scaled_num, (size_t)scale);
    if (ok && scale < 0) ok = big_shift_left(&scaled_den, (size_t)(-scale));
    if (ok) ok = big_divide_round(&scaled_num, &scaled_den, &quotient);
    if (ok) {
        if (normal && big_bit_length(&quotient) > precision) {
            big_shift_right_one(&quotient);
            ++exponent;
        }
        if (exponent > max_exp) {
            literal_inf(sign, low, high);
        } else {
            unsigned int biased;
            if (normal) {
                big_clear_bit(&quotient, precision - 1U);
                biased = (unsigned int)(exponent + 16383);
            } else if (big_bit_length(&quotient) >= precision) {
                big_clear_bit(&quotient, precision - 1U);
                biased = 1U;
            } else {
                biased = 0U;
            }
            ok = big_shift_left(&quotient, 113U - precision);
            if (ok) {
                *low = big_get_u64(&quotient, 0U);
                *high = ((uint64_t)sign << 63U) |
                        ((uint64_t)biased << 48U) |
                        (big_get_u64(&quotient, 2U) & UINT64_C(0x0000ffffffffffff));
            }
        }
    }
    big_dispose(&scaled_num);
    big_dispose(&scaled_den);
    big_dispose(&quotient);
    return ok;
}

static int literal_digit(char c, bool hex) {
    if (c >= '0' && c <= '9') return c - '0';
    if (!hex) return -1;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool minic_binary128_parse_literal(const char *text, size_t length,
                                   unsigned int precision,
                                   uint64_t *low, uint64_t *high) {
    MinicLiteralBig numerator = {0};
    MinicLiteralBig denominator = {0};
    size_t position = 0U;
    size_t fractional_digits = 0U;
    size_t significant_digits = 0U;
    int64_t exponent = 0;
    int64_t decimal_exp;
    unsigned int sign = 0U;
    bool hex;
    bool seen_digit = false;
    bool seen_dot = false;
    bool ok = false;
    char exponent_marker;

    if (text == NULL || low == NULL || high == NULL ||
        (precision != 64U && precision != 113U)) return false;
    if (position < length && (text[position] == '-' || text[position] == '+')) {
        sign = (unsigned int)(text[position] == '-');
        ++position;
    }
    hex = length - position >= 2U && text[position] == '0' &&
          (text[position + 1U] == 'x' || text[position + 1U] == 'X');
    if (hex) position += 2U;
    exponent_marker = hex ? 'p' : 'e';
    while (position < length) {
        int digit = literal_digit(text[position], hex);
        if (digit >= 0) {
            seen_digit = true;
            if (significant_digits != 0U || digit != 0) ++significant_digits;
            if (!big_multiply_add(&numerator, hex ? 16U : 10U,
                                  (uint32_t)digit)) goto done;
            if (seen_dot) ++fractional_digits;
            ++position;
        } else if (text[position] == '.' && !seen_dot) {
            seen_dot = true;
            ++position;
        } else {
            break;
        }
    }
    if (!seen_digit) goto done;
    if (position < length &&
        (text[position] == exponent_marker ||
         text[position] == (char)(exponent_marker - 'a' + 'A'))) {
        bool negative_exp = false;
        bool seen_exp_digit = false;
        ++position;
        if (position < length && (text[position] == '+' || text[position] == '-')) {
            negative_exp = text[position] == '-';
            ++position;
        }
        while (position < length && text[position] >= '0' && text[position] <= '9') {
            seen_exp_digit = true;
            if (exponent < INT64_C(100000000))
                exponent = exponent * 10 + (int64_t)(text[position] - '0');
            ++position;
        }
        if (!seen_exp_digit) goto done;
        if (negative_exp) exponent = -exponent;
    } else if (hex) {
        goto done;
    }
    if (position != length) goto done;
    if (numerator.count == 0U) {
        literal_zero(sign, low, high);
        ok = true;
        goto done;
    }
    if (hex) {
        if (fractional_digits > INT64_MAX / 4U) goto done;
        exponent -= (int64_t)fractional_digits * 4;
        if (exponent > 17000) {
            literal_inf(sign, low, high);
            ok = true;
        } else if (exponent < -200000 && big_bit_length(&numerator) < 100000U) {
            literal_zero(sign, low, high);
            ok = true;
        } else {
            ok = big_from_u32(&denominator, 1U) &&
                 literal_round_rational(&numerator, &denominator, exponent,
                                        precision, sign, low, high);
        }
        goto done;
    }
    if (fractional_digits > INT64_MAX / 2U) goto done;
    decimal_exp = exponent - (int64_t)fractional_digits;
    /* Binary128's entire nonzero dynamic range is inside 10^-5000..10^5000.
     * This early classification also keeps exponent spells like 1e99999999
     * from allocating enormous powers of five. */
    if ((int64_t)significant_digits + decimal_exp > 5000) {
        literal_inf(sign, low, high);
        ok = true;
        goto done;
    }
    if ((int64_t)significant_digits + decimal_exp < -5000) {
        literal_zero(sign, low, high);
        ok = true;
        goto done;
    }
    if (!big_from_u32(&denominator, 1U)) goto done;
    if (decimal_exp >= 0) {
        while (decimal_exp-- > 0) {
            if (!big_multiply_add(&numerator, 5U, 0U)) goto done;
        }
        decimal_exp = exponent - (int64_t)fractional_digits;
    } else {
        int64_t count = -decimal_exp;
        while (count-- > 0) {
            if (!big_multiply_add(&denominator, 5U, 0U)) goto done;
        }
    }
    ok = literal_round_rational(&numerator, &denominator, decimal_exp,
                                precision, sign, low, high);
done:
    big_dispose(&numerator);
    big_dispose(&denominator);
    return ok;
}
