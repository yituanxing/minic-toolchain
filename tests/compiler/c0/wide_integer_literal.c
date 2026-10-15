long long largest_signed(void) {
    return 9223372036854775807LL;
}

long long near_largest_signed(void) {
    return 9223372036854775806LL;
}

int main(void) {
    return largest_signed() > near_largest_signed() ? 0 : 1;
}

enum binary_flags {
    BINARY_FLAG_READ = 0b00000001,
    BINARY_FLAG_WRITE = 0b00000100
};

unsigned int binary_literal_value(void) {
    return 0b10101010U;
}

int binary_enum_value(void) {
    return BINARY_FLAG_READ | BINARY_FLAG_WRITE;
}
