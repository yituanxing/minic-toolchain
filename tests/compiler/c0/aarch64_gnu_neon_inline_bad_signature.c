typedef __Uint8x16_t u8x16;
u8x16 bad_signature(void) {
    return __builtin_aarch64_ld1v16qi_us(42);
}
