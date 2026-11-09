typedef __Int8x8_t i8x8;
typedef __Int16x8_t i16x8;
/* GCC13 includes this widening reduction in the NEON header alongside the
 * six supported wrappers. Source declaration parses, native operation is
 * intentionally not yet selected by MiniC. */
extern __inline __attribute__((__gnu_inline__, __always_inline__))
i16x8 vaddl_s8(i8x8 source) {
    return __builtin_aarch64_saddlv8qi(source);
}
int unrelated(void) { return 17; }
