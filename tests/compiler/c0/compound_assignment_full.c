static int storage = 4;

static int *next_slot(void) {
    return &storage;
}

static int update_once(void) {
    int result = (*next_slot() += 3);
    return result + storage;
}

static int *adjust_pointer(int *pointer) {
    pointer += 3;
    pointer -= 1;
    return pointer;
}

static unsigned int update_bits(unsigned int value) {
    value &= 0xffu;
    value |= 0x100u;
    value ^= 0x3u;
    value >>= 2;
    value *= 3u;
    value -= 1u;
    return value;
}

static unsigned long long divide_unsigned(unsigned long long value) {
    value /= 10;
    return value;
}

static long long divide_signed(long long value) {
    value /= 10;
    return value;
}

static int remainder_int(int value) {
    value %= 1024;
    return value;
}

static unsigned long long remainder_unsigned(unsigned long long value) {
    value %= 9;
    return value;
}

static long long remainder_signed(long long value) {
    value %= 9;
    return value;
}

int main(void) {
    int values[4];
    return update_once() == 14 && adjust_pointer(values) == values + 2 &&
                   update_bits(0x2ffu) == 380u && divide_unsigned(100ULL) == 10ULL &&
                   divide_signed(-100LL) == -10LL && remainder_int(2050) == 2 &&
                   remainder_unsigned(100ULL) == 1ULL && remainder_signed(-100LL) == -1LL
               ? 0
               : 1;
}
