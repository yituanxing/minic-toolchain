typedef unsigned short core_m22_u16;

unsigned int core_m22_or(unsigned int left, unsigned int right) {
    return left | right;
}

unsigned int core_m22_shift_left(unsigned int value, unsigned int count) {
    return value << count;
}

unsigned int core_m22_shift_right_unsigned(unsigned int value, unsigned int count) {
    return value >> count;
}

int core_m22_shift_right_signed(int value, unsigned int count) {
    return value >> count;
}

core_m22_u16 core_m22_fswab16(core_m22_u16 val) {
    return (core_m22_u16)((((core_m22_u16)(val) & (core_m22_u16)0x00ffU) << 8) |
                          (((core_m22_u16)(val) & (core_m22_u16)0xff00U) >> 8));
}
