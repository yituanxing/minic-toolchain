int core_m9_checked_int(int left, int right, int *result) {
    return __builtin_sub_overflow(left, right, result);
}

long core_m9_checked_long(long left, long right, long *result) {
    return __builtin_sub_overflow(left, right, result);
}

unsigned int core_m9_checked_uint(unsigned int left, unsigned int right, unsigned int *result) {
    return __builtin_sub_overflow(left, right, result);
}

unsigned long
core_m9_checked_ulong(unsigned long left, unsigned long right, unsigned long *result) {
    return __builtin_sub_overflow(left, right, result);
}

unsigned long core_m9_size_sub(unsigned long left, unsigned long right) {
    unsigned long bytes;
    if (left == ~0UL || right == ~0UL || __builtin_sub_overflow(left, right, &bytes))
        return ~0UL;
    return bytes;
}
