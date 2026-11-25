typedef __Uint8x16_t u8x16;
extern __inline __attribute__((__gnu_inline__, __always_inline__))
u8x16 bad_cfg(const unsigned char *source) {
    if (source)
        return __builtin_aarch64_ld1v16qi_us(source);
    return __builtin_aarch64_ld1v16qi_us(source);
}
u8x16 caller(const unsigned char *source) {
    return bad_cfg(source);
}
