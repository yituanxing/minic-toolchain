/* Six GCC13 ACLE GNU extern/always-inline wrappers, deliberately retaining
 * genuine builtin spelling and GNU linkage. This subset does not claim all
 * arm_neon.h functions are implemented. */
typedef __Uint8x16_t uint8x16_t;
typedef __Uint64x2_t uint64x2_t;
typedef unsigned char uint8_t;
typedef unsigned long uint64_t;

__extension__ extern __inline __attribute__((__gnu_inline__, __always_inline__, __artificial__))
uint8x16_t vld1q_u8(const uint8_t *__a) {
    return (uint8x16_t) __builtin_aarch64_ld1v16qi_us(
        (const __builtin_aarch64_simd_qi *) __a);
}
__extension__ extern __inline __attribute__((__gnu_inline__, __always_inline__, __artificial__))
uint64x2_t vld1q_u64(const uint64_t *__a) {
    return (uint64x2_t) __builtin_aarch64_ld1v2di_us(
        (const __builtin_aarch64_simd_di *) __a);
}
__extension__ extern __inline __attribute__((__gnu_inline__, __always_inline__, __artificial__))
void vst1q_u8(uint8_t *__a, uint8x16_t __b) {
    __builtin_aarch64_st1v16qi_su((__builtin_aarch64_simd_qi *) __a, __b);
}
__extension__ extern __inline __attribute__((__gnu_inline__, __always_inline__, __artificial__))
void vst1q_u64(uint64_t *__a, uint64x2_t __b) {
    __builtin_aarch64_st1v2di_su((__builtin_aarch64_simd_di *) __a, __b);
}
__extension__ extern __inline __attribute__((__gnu_inline__, __always_inline__, __artificial__))
uint8x16_t veorq_u8(uint8x16_t __a, uint8x16_t __b) {
    return __a ^ __b;
}
__extension__ extern __inline __attribute__((__gnu_inline__, __always_inline__, __artificial__))
uint64x2_t veorq_u64(uint64x2_t __a, uint64x2_t __b) {
    return __a ^ __b;
}

/* Each pointer is a runtime parameter; no compile-time constant shortcut. */
void xor8(uint8_t *dest, const uint8_t *left, const uint8_t *right) {
    uint8x16_t a = vld1q_u8(left);
    uint8x16_t b = vld1q_u8(right);
    vst1q_u8(dest, veorq_u8(a, b));
}
void xor64(uint64_t *dest, const uint64_t *left,
           const uint64_t *right) {
    uint64x2_t a = vld1q_u64(left);
    uint64x2_t b = vld1q_u64(right);
    vst1q_u64(dest, veorq_u64(a, b));
}

/* An effectful argument must be evaluated exactly once through the inline
 * wrapper, even if the nested builtin consumes a load and returns a vector. */
extern const uint8_t *next8(void);
void dynamic_argument_once(uint8_t *dest) {
    vst1q_u8(dest, vld1q_u8(next8()));
}
