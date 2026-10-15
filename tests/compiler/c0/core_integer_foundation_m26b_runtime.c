unsigned long m26b_arithmetic(unsigned long a, unsigned long b);
long m26b_signed_divrem(long a, long b);
int m26b_relations(unsigned long a, unsigned long b);
unsigned long m26b_shift_cfg(unsigned long a, unsigned long *p);
unsigned long m26b_pointer_offset_cfg(const unsigned long *p, unsigned long *q);
unsigned long m26b_multiarg_cfg(unsigned long *p, unsigned long *q);
void m26b_compound_cfg(unsigned long *p, unsigned long *q);
void m26b_assignment_cfg(unsigned long *p, unsigned long *q, int choose);
int m26b_overflow_cfg(unsigned long *out, unsigned long *p, unsigned long *q);
void m26b_cpu_to_be32_array(unsigned int *dst, const unsigned int *src, unsigned long len);
unsigned int m26b_reciprocal_scale(unsigned int val, unsigned int ep_ro);
int m26b_in_range64(unsigned long val, unsigned long start, unsigned long len);
int m26b_is_err(const void *ptr);
void m26b_set_bit(unsigned long nr, volatile unsigned long *addr);

int main(void) {
    unsigned long p;
    unsigned long q;
    unsigned long out;
    unsigned long words[4] = {10UL, 20UL, 30UL, 40UL};
    unsigned int src[3] = {0x11223344U, 0xaabbccddU, 0x01020304U};
    unsigned int dst[3] = {0U, 0U, 0U};
    volatile unsigned long bits[2] = {0UL, 0UL};

    if (m26b_arithmetic(20UL, 6UL) != 157UL) {
        return 1;
    }
    if (m26b_signed_divrem(-20L, 6L) != -5L) {
        return 2;
    }
    if (m26b_relations(3UL, 5UL) != 3 || m26b_relations(5UL, 3UL) != 12 ||
        m26b_relations(5UL, 5UL) != 10) {
        return 3;
    }

    p = 2UL;
    if (m26b_shift_cfg(1UL, &p) != 8UL) {
        return 4;
    }
    q = 1UL;
    if (m26b_pointer_offset_cfg(words, &q) != 30UL) {
        return 5;
    }
    p = 3UL;
    q = 5UL;
    if (m26b_multiarg_cfg(&p, &q) != 10UL) {
        return 6;
    }
    p = 0xf0UL;
    q = 0x0fUL;
    m26b_compound_cfg(&p, &q);
    if (p != 0x10UL) {
        return 7;
    }
    q = 5UL;
    m26b_assignment_cfg(words, &q, 1);
    if (words[1] != 6UL) {
        return 8;
    }

    p = ~0UL - 1UL;
    q = 0UL;
    out = 99UL;
    if (!m26b_overflow_cfg(&out, &p, &q) || out != 0UL) {
        return 9;
    }

    m26b_cpu_to_be32_array(dst, src, 3UL);
    if (dst[0] != 0x44332211U || dst[1] != 0xddccbbaaU || dst[2] != 0x04030201U) {
        return 10;
    }
    if (m26b_reciprocal_scale(0x80000000U, 2U) != 1U) {
        return 11;
    }
    if (!m26b_in_range64(15UL, 10UL, 10UL) || m26b_in_range64(25UL, 10UL, 10UL)) {
        return 12;
    }
    if (!m26b_is_err((const void *)~0UL) || m26b_is_err((const void *)0x1000UL)) {
        return 13;
    }

    m26b_set_bit(65UL, bits);
    if (bits[0] != 0UL || bits[1] != 2UL) {
        return 14;
    }
    return 0;
}
