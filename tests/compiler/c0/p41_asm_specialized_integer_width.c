/* P41: GNU `i` after call specialization must obey typed C expression
 * semantics, including casts, boolean conversion, unsigned wrap and shifts.
 * Distinct top-level callers keep all variants alive in GCC and MiniC. */
__attribute__((always_inline)) static inline int emit_cast(unsigned int value) {
    __asm__ volatile(".long %c0" :: "i"((unsigned char)(value + 1U)));
    return 0;
}
__attribute__((always_inline)) static inline int emit_signed(unsigned int value) {
    __asm__ volatile(".long %c0" :: "i"((signed char)(value + 1U)));
    return 0;
}
__attribute__((always_inline)) static inline int emit_boolean(unsigned int value) {
    __asm__ volatile(".long %c0" :: "i"((_Bool)(value + 1U)));
    return 0;
}
__attribute__((always_inline)) static inline int emit_wrapping(unsigned int value) {
    __asm__ volatile(".long %c0" :: "i"((unsigned int)(value + 2U)));
    return 0;
}
__attribute__((always_inline)) static inline int emit_shift_after_cast(unsigned int value) {
    __asm__ volatile(".long %c0" :: "i"(((unsigned char)(value + 1U)) << 1));
    return 0;
}
__attribute__((always_inline)) static inline int emit_unsigned_short(unsigned int value) {
    __asm__ volatile(".long %c0" :: "i"((unsigned short)(value + 2U)));
    return 0;
}
__attribute__((always_inline)) static inline int emit_signed_short(unsigned int value) {
    __asm__ volatile(".long %c0" :: "i"((short)(value + 1U)));
    return 0;
}
__attribute__((always_inline)) static inline int emit_negate(unsigned int value) {
    __asm__ volatile(".long %c0" :: "i"(-((int)(value + 1U))));
    return 0;
}
__attribute__((always_inline)) static inline int emit_complement(unsigned int value) {
    __asm__ volatile(".long %c0" :: "i"(~((unsigned short)(value + 1U))));
    return 0;
}
__attribute__((always_inline)) static inline int emit_narrow_shift(unsigned int value) {
    __asm__ volatile(".long %c0" :: "i"(((unsigned short)(value + 1U)) >> 1));
    return 0;
}
__attribute__((always_inline)) static inline int emit_unsigned_underflow(unsigned int value) {
    __asm__ volatile(".long %c0" :: "i"(value - 2U));
    return 0;
}
__attribute__((always_inline)) static inline int emit_logical_not(unsigned int value) {
    __asm__ volatile(".long %c0" :: "i"(!(value + 1U)));
    return 0;
}
int p41_cast(void) { return emit_cast(258U); }
int p41_signed(void) { return emit_signed(254U); }
int p41_boolean(void) { return emit_boolean(258U); }
int p41_wrapping(void) { return emit_wrapping(4294967295U); }
int p41_shift_after_cast(void) { return emit_shift_after_cast(255U); }
int p41_unsigned_short(void) { return emit_unsigned_short(65535U); }
int p41_signed_short(void) { return emit_signed_short(65534U); }
int p41_negate(void) { return emit_negate(6U); }
int p41_complement(void) { return emit_complement(65534U); }
int p41_narrow_shift(void) { return emit_narrow_shift(65535U); }
int p41_unsigned_underflow(void) { return emit_unsigned_underflow(0U); }
int p41_logical_not(void) { return emit_logical_not(4294967295U); }
