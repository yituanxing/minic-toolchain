static long sum9(long a0, long a1, long a2, long a3, long a4,
                 long a5, long a6, long a7, long a8) {
    return a0 + a1 + a2 + a3 + a4 + a5 + a6 + a7 + a8;
}

int builtin_alloca_probe(unsigned long n) {
    unsigned char *p;
    unsigned char *q;
    unsigned long i;
    long call_result;
    int fixed;

    fixed = 17;
    p = __builtin_alloca(n);
    if (((unsigned long)p & 15UL) != 0UL)
        return 1;
    for (i = 0; i < n; ++i)
        p[i] = (unsigned char)(i + 3UL);

    fixed = fixed + 5;
    call_result = sum9(1, 2, 3, 4, 5, 6, 7, 8, 9);
    if (call_result != 45 || fixed != 22)
        return 2;
    for (i = 0; i < n; ++i) {
        if (p[i] != (unsigned char)(i + 3UL))
            return 3;
    }

    q = __builtin_alloca(33UL);
    if (((unsigned long)q & 15UL) != 0UL)
        return 4;
    q[0] = 91;
    q[32] = 123;
    if (sum9(9, 8, 7, 6, 5, 4, 3, 2, 1) != 45)
        return 5;
    if (q[0] != 91 || q[32] != 123)
        return 6;
    if (n != 0UL && p[n - 1UL] != (unsigned char)(n + 2UL))
        return 7;
    return fixed == 22 ? 0 : 8;
}
