static int runtime_clzll_ull(unsigned long long value) {
    return __builtin_clzll(value);
}

static int runtime_clzll_uint(unsigned int value) {
    return __builtin_clzll(value);
}

static int runtime_clzll_int(int value) {
    return __builtin_clzll(value);
}

int main(void) {
    return runtime_clzll_ull(1ULL) == 63 &&
                   runtime_clzll_ull(16ULL) == 59 &&
                   runtime_clzll_ull(0x8000000000000000ULL) == 0 &&
                   runtime_clzll_ull(0x00f0000000000000ULL) == 8 &&
                   runtime_clzll_uint(0x80000000U) == 32 &&
                   runtime_clzll_int(-1) == 0
               ? 0
               : 1;
}
