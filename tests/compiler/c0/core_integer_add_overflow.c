int core_m8_checked_int(int left, int right, int *result) {
    return __builtin_add_overflow(left, right, result);
}

long core_m8_checked_long(long left, long right, long *result) {
    return __builtin_add_overflow(left, right, result);
}

unsigned int core_m8_checked_uint(unsigned int left, unsigned int right, unsigned int *result) {
    return __builtin_add_overflow(left, right, result);
}

unsigned long
core_m8_checked_ulong(unsigned long left, unsigned long right, unsigned long *result) {
    return __builtin_add_overflow(left, right, result);
}

unsigned long core_m8_size_add(unsigned long left, unsigned long right) {
    unsigned long bytes;
    if (__builtin_add_overflow(left, right, &bytes))
        return ~0UL;
    return bytes;
}
