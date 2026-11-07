/* 8-bit ld1 must not accept a 64-bit internal builtin scalar pointer. */
typedef __Uint8x16_t uint8x16_t;
uint8x16_t bad_qi(const unsigned char *address) {
    return __builtin_aarch64_ld1v16qi_us(
        (const __builtin_aarch64_simd_di *) address);
}
