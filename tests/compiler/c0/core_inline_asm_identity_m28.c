unsigned long core_m28_inline_asm_identity(unsigned long value) {
    __asm__("" : "+rm"(value));
    return value;
}

unsigned int core_m28_iter_div_u64_rem(unsigned long dividend,
                                       unsigned int divisor,
                                       unsigned long *remainder) {
    unsigned int ret;

    ret = 0;
    while (dividend >= divisor) {
        __asm__("" : "+rm"(dividend));
        dividend -= divisor;
        ret++;
    }
    *remainder = dividend;
    return ret;
}

unsigned long core_m28_compound_xor(unsigned long *value, unsigned long mask) {
    *value ^= mask;
    return *value;
}
