static __inline__ unsigned short swap16(unsigned short value) {
    return (unsigned short)((value << 8) | (value >> 8));
}

static __inline unsigned int add1(unsigned int value) {
    return value + 1U;
}

int main(void) {
    return (swap16(0x1234U) == 0x3412U && add1(41U) == 42U) ? 0 : 1;
}
