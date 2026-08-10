static _Bool __attribute__((__warn_unused_result__)) checked_add_int(int a, int b, int *result) {
    return __builtin_add_overflow(a, b, result);
}

static _Bool checked_mul_long(long a, long b, long *result) {
    return __builtin_mul_overflow(a, b, result);
}

static _Bool checked_mul_ulong(unsigned long a, unsigned long b, unsigned long *result) {
    return __builtin_mul_overflow(a, b, result);
}

static _Bool checked_sub_ulong(unsigned long a, unsigned long b, unsigned long *result) {
    return __builtin_sub_overflow(a, b, result);
}

int main(void) {
    int i = 0;
    long l = 0;
    unsigned long ul = 0;

    return checked_add_int(1, 2, &i) || checked_mul_long(2, 3, &l) ||
                   checked_mul_ulong(4UL, 5UL, &ul) || checked_sub_ulong(8UL, 3UL, &ul)
               ? 1
               : (i == 3 && l == 6 && ul == 5UL ? 0 : 2);
}
