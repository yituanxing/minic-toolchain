typedef __Uint8x16_t u8x16;
extern u8x16 __minic_target_builtin_aarch64_ld1v16qi_us(const unsigned char *);
u8x16 caller(const unsigned char *source) {
    return __minic_target_builtin_aarch64_ld1v16qi_us(source);
}
