static unsigned long m23_swab64(unsigned long x) {
    return ((x & 0x00000000000000ffUL) << 56) | ((x & 0x000000000000ff00UL) << 40) |
           ((x & 0x0000000000ff0000UL) << 24) | ((x & 0x00000000ff000000UL) << 8) |
           ((x & 0x000000ff00000000UL) >> 8) | ((x & 0x0000ff0000000000UL) >> 24) |
           ((x & 0x00ff000000000000UL) >> 40) | ((x & 0xff00000000000000UL) >> 56);
}

unsigned long core_m23_choose(int condition, unsigned long when_true, unsigned long when_false) {
    return condition ? when_true : when_false;
}

unsigned long core_m23_swab_shape(unsigned long y) {
    return (unsigned long)(__builtin_constant_p(y) ? (((y & 0x00000000000000ffUL) << 56) |
                                                      ((y & 0x000000000000ff00UL) << 40) |
                                                      ((y & 0x0000000000ff0000UL) << 24) |
                                                      ((y & 0x00000000ff000000UL) << 8) |
                                                      ((y & 0x000000ff00000000UL) >> 8) |
                                                      ((y & 0x0000ff0000000000UL) >> 24) |
                                                      ((y & 0x00ff000000000000UL) >> 40) |
                                                      ((y & 0xff00000000000000UL) >> 56))
                                                   : m23_swab64(y));
}
