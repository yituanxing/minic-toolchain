static _Bool core_m6_must_check(_Bool overflow) {
    return overflow;
}

int core_m6_checked_int(int left, int right, int *result) {
    return __builtin_mul_overflow(left, right, result);
}

long core_m6_checked_long(long left, long right, long *result) {
    return __builtin_mul_overflow(left, right, result);
}

unsigned long
core_m6_checked_ulong(unsigned long left, unsigned long right, unsigned long *result) {
    return __builtin_mul_overflow(left, right, result);
}

unsigned long core_m6_size_mul(unsigned long left, unsigned long right) {
    unsigned long bytes;

    if (core_m6_must_check(__builtin_mul_overflow(left, right, &bytes)))
        return 99UL;
    return bytes;
}
