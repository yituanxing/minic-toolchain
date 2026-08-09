enum MiniMode {
    MINI_MODE_ZERO = 0,
    MINI_MODE_TWO = 2
};

const unsigned char bitwise_table[(1 << 3)] = {
    ((0 << 7) | (1 << 3) | MINI_MODE_TWO),
    ((7 & 3) ^ 4),
    (~0 & 0xff),
    (!0 << 1),
    (32 >> 2)
};
