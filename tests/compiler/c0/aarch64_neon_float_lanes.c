/* AArch64 ACLE vector identity and AAPCS64 SIMD register carriers.
 * The element kind matters: none of these are integer ADDs. */
typedef __Float32x2_t f32x2;
typedef __Float64x1_t f64x1;
typedef __Float32x4_t f32x4;
typedef __Float64x2_t f64x2;

f32x2 add_f32x2(f32x2 a, f32x2 b) { return a + b; }
f64x1 add_f64x1(f64x1 a, f64x1 b) { return a + b; }
f32x4 add_f32x4(f32x4 a, f32x4 b) { return a + b; }
f64x2 add_f64x2(f64x2 a, f64x2 b) { return a + b; }

__attribute__((noinline)) f32x2 ninth_f32x2(
    f32x2 a, f32x2 b, f32x2 c, f32x2 d, f32x2 e,
    f32x2 f, f32x2 g, f32x2 h, f32x2 i) {
    return a + i;
}

__attribute__((noinline)) f64x2 invoke_f64x2(
    f64x2 (*fn)(f64x2, f64x2), f64x2 x, f64x2 y) {
    return fn(x, y);
}
