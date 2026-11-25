#ifndef MINIC_COMMON_INTEGER_BITS_H
#define MINIC_COMMON_INTEGER_BITS_H

#include <stdint.h>

/* Pure bit geometry; language types, signedness and overflow policy belong
 * to callers. Width 0 is an empty mask; widths >=64 saturate. */
static inline uint64_t minic_integer_mask64(unsigned int width) {
    return width >= 64U ? UINT64_MAX : (UINT64_C(1) << width) - UINT64_C(1);
}

/* Sign extend the low bits. Callers must establish a nonzero signed width.
 * Widths >=64 already occupy the complete payload. */
static inline uint64_t minic_integer_sign_extend64(uint64_t bits,
                                                   unsigned int width) {
    if (width == 0U || width >= 64U) return bits;
    const uint64_t mask = minic_integer_mask64(width);
    bits &= mask;
    return (bits & (UINT64_C(1) << (width - 1U))) != 0U ? bits | ~mask : bits;
}

#endif
