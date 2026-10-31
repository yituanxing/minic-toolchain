/* Exact GCC ACLE source used as the independent execution oracle.
 * MiniC must eventually compile this source (including the real system
 * arm_neon.h) and produce the identical observable bytes under QEMU. */
#include <arm_neon.h>
#include <stdint.h>

static void xor_bytes(uint8_t *dest, const uint8_t *left,
                      const uint8_t *right) {
    uint8x16_t a = vld1q_u8(left);
    uint8x16_t b = vld1q_u8(right);
    vst1q_u8(dest, veorq_u8(a, b));
}

static void xor_words(uint64_t *dest, const uint64_t *left,
                      const uint64_t *right) {
    uint64x2_t a = vld1q_u64(left);
    uint64x2_t b = vld1q_u64(right);
    vst1q_u64(dest, veorq_u64(a, b));
}

int main(void) {
    uint8_t left[16], right[16], dest[16];
    uint64_t wide_left[2] = {UINT64_C(0x123456789abcdef0),
                             UINT64_C(0xdeadbeef00440088)};
    uint64_t wide_right[2] = {UINT64_C(0xff00ff00ff00ff00),
                              UINT64_C(0x0011223344556677)};
    uint64_t wide_dest[2] = {0, 0};
    unsigned int i;

    for (i = 0; i < 16; ++i) {
        left[i] = (uint8_t)(i * 13U + 5U);
        right[i] = (uint8_t)(i * 7U + 9U);
        dest[i] = 0U;
    }
    xor_bytes(dest, left, right);
    for (i = 0; i < 16; ++i)
        if (dest[i] != (uint8_t)(left[i] ^ right[i])) return 1;

    xor_words(wide_dest, wide_left, wide_right);
    for (i = 0; i < 2; ++i)
        if (wide_dest[i] != (wide_left[i] ^ wide_right[i])) return 2;

    return 0;
}
