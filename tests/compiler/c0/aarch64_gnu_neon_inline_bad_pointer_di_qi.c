/* 64-bit ld1 must not accept an 8-bit internal builtin scalar pointer. */
typedef __Uint64x2_t uint64x2_t;
uint64x2_t bad_di(const unsigned long *address) {
    return __builtin_aarch64_ld1v2di_us(
        (const __builtin_aarch64_simd_qi *) address);
}
