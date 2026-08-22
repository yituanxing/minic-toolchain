int core_m26_signed_less(long left, long right) {
    return left < right;
}

int core_m26_unsigned_less(unsigned long left, unsigned long right) {
    return left < right;
}

int core_m26_mixed_less(unsigned int left, unsigned long right) {
    return left < right;
}

void core_m26_array_loop(unsigned int *dst, const unsigned int *src, unsigned long len) {
    unsigned long i;

    for (i = 0; i < len; i++) {
        dst[i] = src[i];
    }
}
