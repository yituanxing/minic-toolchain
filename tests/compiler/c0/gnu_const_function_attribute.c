static inline __attribute__((__gnu_inline__)) __attribute__((__unused__)) __attribute__((__no_instrument_function__)) __attribute__((__const__)) unsigned int fswab_like(unsigned int value) {
    return ((value & 0x00ffU) << 8) | ((value & 0xff00U) >> 8);
}

int main(void) {
    return fswab_like(0x1234U) == 0x3412U ? 0 : 1;
}
