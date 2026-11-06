/* Integer/polynomial Q-register lane families: exact lane arithmetic and
 * canonical bitwise XOR, with distinct AAPCS64 Q-register identities. */
typedef __Int16x8_t q16;
typedef __Int32x4_t q32;
typedef __Int64x2_t q64;
typedef __Uint16x8_t uq16;
typedef __Uint32x4_t uq32;
typedef __Uint64x2_t uq64;
typedef __Poly16x8_t pq16;
typedef __Poly64x2_t pq64;

q16 qadd16(q16 x, q16 y) { return x + y; }
q32 qadd32(q32 x, q32 y) { return x + y; }
q64 qadd64(q64 x, q64 y) { return x + y; }
uq16 qxor16(uq16 x, uq16 y) { return x ^ y; }
uq32 qxor32(uq32 x, uq32 y) { return x ^ y; }
uq64 qxor64(uq64 x, uq64 y) { return x ^ y; }
pq16 qpxor16(pq16 x, pq16 y) { return x ^ y; }
pq64 qpxor64(pq64 x, pq64 y) { return x ^ y; }

__attribute__((noinline)) q16 qninth16(
    q16 a, q16 b, q16 c, q16 d, q16 e, q16 f, q16 g, q16 h, q16 i) {
    return a + i;
}
