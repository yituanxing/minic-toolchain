unsigned long m26_arithmetic(unsigned long a, unsigned long b);
long m26_signed_divrem(long a, long b);
int m26_relations(unsigned long a, unsigned long b);
int m26_signed_less(long a, long b);
unsigned long m26_shift_cfg(unsigned long a, unsigned long *p);
unsigned long m26_multiarg_cfg(unsigned long *p, unsigned long *q);
void m26_compound_cfg(unsigned long *p, unsigned long *q);
void m26_subscript_store(unsigned long *p, int choose, unsigned long value);
unsigned long m26_subscript_load(const unsigned long *p, unsigned long i);
void m26_cpu_to_be32_array(unsigned int *dst, const unsigned int *src, unsigned long len);
unsigned int m26_reciprocal_scale(unsigned int val, unsigned int ep_ro);
int m26_in_range64(unsigned long val, unsigned long start, unsigned long len);
int m26_is_err(const void *ptr);
void m26_set_bit(unsigned long nr, volatile unsigned long *addr);

int main(void) {
    unsigned long p;
    unsigned long q;
    unsigned long words[4] = {10UL, 20UL, 30UL, 40UL};
    unsigned int src[3] = {0x11223344U, 0xaabbccddU, 0x01020304U};
    unsigned int dst[3] = {0U, 0U, 0U};
    volatile unsigned long bits[2] = {0UL, 0UL};

    if (m26_arithmetic(20UL, 6UL) != 157UL) {
        return 1;
    }
    if (m26_signed_divrem(-20L, 6L) != -5L) {
        return 2;
    }
    if (m26_relations(3UL, 5UL) != 3 || m26_relations(5UL, 3UL) != 12 ||
        m26_relations(5UL, 5UL) != 10) {
        return 3;
    }
    if (!m26_signed_less(-7L, 3L) || m26_signed_less(9L, -2L)) {
        return 4;
    }

    p = 2UL;
    if (m26_shift_cfg(1UL, &p) != 8UL) {
        return 5;
    }
    p = 3UL;
    q = 5UL;
    if (m26_multiarg_cfg(&p, &q) != 10UL) {
        return 6;
    }
    p = 0xf0UL;
    q = 0x0fUL;
    m26_compound_cfg(&p, &q);
    if (p != 0x10UL) {
        return 7;
    }

    m26_subscript_store(words, 1, 99UL);
    m26_subscript_store(words, 0, 77UL);
    if (words[1] != 99UL || words[2] != 77UL || m26_subscript_load(words, 3UL) != 40UL) {
        return 8;
    }

    m26_cpu_to_be32_array(dst, src, 3UL);
    if (dst[0] != 0x44332211U || dst[1] != 0xddccbbaaU || dst[2] != 0x04030201U) {
        return 9;
    }
    if (m26_reciprocal_scale(0x80000000U, 2U) != 1U) {
        return 10;
    }
    if (!m26_in_range64(15UL, 10UL, 10UL) || m26_in_range64(25UL, 10UL, 10UL)) {
        return 11;
    }
    if (!m26_is_err((const void *)~0UL) || m26_is_err((const void *)0x1000UL)) {
        return 12;
    }

    m26_set_bit(65UL, bits);
    if (bits[0] != 0UL || bits[1] != 2UL) {
        return 13;
    }
    return 0;
}
