/* Independent executable GCC ACLE/SHA3 oracle. Run with QEMU CPU `max`,
 * because EOR3 requires FEAT_SHA3; GNU's `.arch_extension` is an assembler
 * permission and does not itself guarantee runtime CPU support. */
#include <arm_neon.h>
#include <stdint.h>

static uint64x2_t xor3(uint64x2_t p, uint64x2_t q, uint64x2_t r) {
    uint64x2_t result;
    __asm__(".arch_extension sha3\n"
            "eor3 %0.16b, %1.16b, %2.16b, %3.16b"
            : "=w"(result) : "w"(p), "w"(q), "w"(r));
    return result;
}

int main(void) {
    uint64_t p[2] = {UINT64_C(0x0123456789abcdef),
                     UINT64_C(0x0123456789abcdef)};
    uint64_t q[2] = {UINT64_C(0xfedcba9876543210),
                     UINT64_C(0xffff0000aaaa5555)};
    uint64_t r[2] = {UINT64_C(0x123456789abcdef0),
                     UINT64_C(0x123456789abcdef0)};
    uint64_t result[2] = {0, 0};
    vst1q_u64(result, xor3(vld1q_u64(p), vld1q_u64(q), vld1q_u64(r)));
    return (result[0] != (p[0] ^ q[0] ^ r[0]) ||
            result[1] != (p[1] ^ q[1] ^ r[1]));
}
