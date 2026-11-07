#pragma GCC push_options
#pragma GCC target ("+nothing+simd")
typedef __Int16x4_t int16x4_t;
typedef __Int32x4_t int32x4_t;
__extension__ extern __inline int32x4_t
__attribute__((__always_inline__, __gnu_inline__, __artificial__))
fake_header_wrapper(int16x4_t a, int16x4_t b) {
    return __builtin_aarch64_saddlv4hi(a, b);
}
#pragma GCC pop_options
int main(void) { return 17; }
