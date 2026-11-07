/* All integer/polynomial D-register lane layouts share the same AAPCS64
 * eight-byte register carrier, but ADD must preserve element boundaries.
 * Keep each source identity distinct: no byte-vector ABI substitutions. */
typedef __Int8x8_t d8;
typedef __Int16x4_t d16;
typedef __Int32x2_t d32;
typedef __Int64x1_t d64;
typedef __Uint16x4_t u16;
typedef __Uint32x2_t u32;
typedef __Uint64x1_t u64;
typedef __Poly8x8_t p8;
typedef __Poly16x4_t p16;
typedef __Poly64x1_t p64;

d8 add8(d8 x, d8 y) { return x + y; }
d16 add16(d16 x, d16 y) { return x + y; }
d32 add32(d32 x, d32 y) { return x + y; }
d64 add64(d64 x, d64 y) { return x + y; }
u16 xor16(u16 x, u16 y) { return x ^ y; }
u32 xor32(u32 x, u32 y) { return x ^ y; }
u64 xor64(u64 x, u64 y) { return x ^ y; }
p8 pxor8(p8 x, p8 y) { return x ^ y; }
p16 pxor16(p16 x, p16 y) { return x ^ y; }
p64 pxor64(p64 x, p64 y) { return x ^ y; }

/* Ensure each ABI vector type is returned through v0, not GPR pairs. */
__attribute__((noinline)) d16 ninth16(
    d16 a, d16 b, d16 c, d16 d, d16 e, d16 f, d16 g, d16 h, d16 i) {
    return a + i;
}
__attribute__((noinline)) d32 invoke32(d32 (*fn)(d32,d32), d32 x, d32 y) {
    return fn(x,y);
}
