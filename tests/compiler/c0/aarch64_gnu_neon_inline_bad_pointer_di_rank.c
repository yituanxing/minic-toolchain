/* GCC13 LP64 intDI is canonical signed long, not signed long long. */
typedef __Uint64x2_t uint64x2_t;
uint64x2_t bad_di_rank(const long long *address) {
    return __builtin_aarch64_ld1v2di_us(address);
}
