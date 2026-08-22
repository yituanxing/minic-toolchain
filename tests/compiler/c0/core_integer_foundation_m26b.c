unsigned long m26b_slow(unsigned long v) {
    return v + 1UL;
}

unsigned long m26b_sink(unsigned long a, unsigned long b) {
    return a + b;
}

unsigned long m26b_arithmetic(unsigned long a, unsigned long b) {
    return (a - b) + (a * b) + (a / b) + (a % b) + (a ^ b);
}

long m26b_signed_divrem(long a, long b) {
    return (a / b) + (a % b);
}

int m26b_relations(unsigned long a, unsigned long b) {
    return (a < b) | ((a <= b) << 1) | ((a > b) << 2) | ((a >= b) << 3);
}

unsigned long m26b_shift_cfg(unsigned long a, unsigned long *p) {
    return a << (__builtin_constant_p(*p) ? *p : m26b_slow(*p));
}

unsigned long m26b_pointer_offset_cfg(const unsigned long *p, unsigned long *q) {
    return *(p + (__builtin_constant_p(*q) ? *q : m26b_slow(*q)));
}

unsigned long m26b_multiarg_cfg(unsigned long *p, unsigned long *q) {
    return m26b_sink(__builtin_constant_p(*p) ? *p : m26b_slow(*p),
                     __builtin_constant_p(*q) ? *q : m26b_slow(*q));
}

void m26b_compound_cfg(unsigned long *p, unsigned long *q) {
    *p &= (__builtin_constant_p(*q) ? *q : m26b_slow(*q));
    *p |= (__builtin_constant_p(*q) ? *q : m26b_slow(*q));
}

void m26b_assignment_cfg(unsigned long *p, unsigned long *q, int choose) {
    p[choose ? 1UL : 2UL] = __builtin_constant_p(*q) ? *q : m26b_slow(*q);
}

int m26b_overflow_cfg(unsigned long *out, unsigned long *p, unsigned long *q) {
    return __builtin_add_overflow(__builtin_constant_p(*p) ? *p : m26b_slow(*p),
                                  __builtin_constant_p(*q) ? *q : m26b_slow(*q),
                                  out);
}

unsigned int m26b_fswab32(unsigned int v) {
    return ((v & 0x000000ffU) << 24) | ((v & 0x0000ff00U) << 8) | ((v & 0x00ff0000U) >> 8) |
           ((v & 0xff000000U) >> 24);
}

void m26b_cpu_to_be32_array(unsigned int *dst, const unsigned int *src, unsigned long len) {
    unsigned long i;

    for (i = 0; i < len; i++) {
        dst[i] = __builtin_constant_p(src[i])
                     ? (((src[i] & 0x000000ffU) << 24) | ((src[i] & 0x0000ff00U) << 8) |
                        ((src[i] & 0x00ff0000U) >> 8) | ((src[i] & 0xff000000U) >> 24))
                     : m26b_fswab32(src[i]);
    }
}

unsigned int m26b_reciprocal_scale(unsigned int val, unsigned int ep_ro) {
    return (unsigned int)(((unsigned long)val * ep_ro) >> 32);
}

int m26b_in_range64(unsigned long val, unsigned long start, unsigned long len) {
    return (val - start) < len;
}

int m26b_is_err(const void *ptr) {
    return !!((unsigned long)ptr >= (unsigned long)-4095L);
}

void m26b_set_bit(unsigned long nr, volatile unsigned long *addr) {
    unsigned long mask = 1UL << (nr % 64UL);
    unsigned long *p = (unsigned long *)addr + (nr / 64UL);

    *p |= mask;
}
