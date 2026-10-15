static unsigned short core_m25b_slow(unsigned short value) {
    return (unsigned short)(value + 1U);
}

unsigned short core_m25b_add(unsigned short *left, unsigned short *right) {
    return (unsigned short)((__builtin_constant_p(*left) ? *left : core_m25b_slow(*left)) +
                            (__builtin_constant_p(*right) ? *right : core_m25b_slow(*right)));
}

unsigned short core_m25b_and(unsigned short *left, unsigned short *right) {
    return (unsigned short)((__builtin_constant_p(*left) ? *left : core_m25b_slow(*left)) &
                            (__builtin_constant_p(*right) ? *right : core_m25b_slow(*right)));
}

unsigned short core_m25b_or(unsigned short *left, unsigned short *right) {
    return (unsigned short)((__builtin_constant_p(*left) ? *left : core_m25b_slow(*left)) |
                            (__builtin_constant_p(*right) ? *right : core_m25b_slow(*right)));
}

void core_m25b_be16_shape(unsigned short *value, unsigned short addend) {
    *value =
        __builtin_constant_p((unsigned short)((__builtin_constant_p(*value)
                                                   ? (unsigned short)(((*value & 0x00ffU) << 8) |
                                                                      ((*value & 0xff00U) >> 8))
                                                   : core_m25b_slow(*value)) +
                                              addend))
            ? (unsigned short)(((((unsigned short)((__builtin_constant_p(*value)
                                                        ? (unsigned short)(((*value & 0x00ffU)
                                                                            << 8) |
                                                                           ((*value & 0xff00U) >>
                                                                            8))
                                                        : core_m25b_slow(*value)) +
                                                   addend)) &
                                 0x00ffU)
                                << 8) |
                               ((((unsigned short)((__builtin_constant_p(*value)
                                                        ? (unsigned short)(((*value & 0x00ffU)
                                                                            << 8) |
                                                                           ((*value & 0xff00U) >>
                                                                            8))
                                                        : core_m25b_slow(*value)) +
                                                   addend)) &
                                 0xff00U) >>
                                8))
            : core_m25b_slow((unsigned short)((__builtin_constant_p(*value)
                                                   ? (unsigned short)(((*value & 0x00ffU) << 8) |
                                                                      ((*value & 0xff00U) >> 8))
                                                   : core_m25b_slow(*value)) +
                                              addend));
}
