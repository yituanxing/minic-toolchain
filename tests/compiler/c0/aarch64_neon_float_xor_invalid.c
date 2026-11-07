/* Floating SIMD lanes do not permit bitwise XOR on C vector operands. */
typedef __Float32x2_t f32x2;
f32x2 bad_float_xor(f32x2 a, f32x2 b) { return a ^ b; }
