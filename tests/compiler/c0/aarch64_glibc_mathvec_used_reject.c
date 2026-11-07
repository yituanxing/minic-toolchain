typedef __Float32x4_t __f32x4_t;
__attribute__((__aarch64_vector_pcs__)) __f32x4_t _ZGVnN4v_sinf(__f32x4_t);
__f32x4_t call_vector_pcs(__f32x4_t value) {
    return _ZGVnN4v_sinf(value);
}
