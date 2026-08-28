_Static_assert(__builtin_clz(1U) == 31, "clz one");
_Static_assert(__builtin_clzl(1UL) == 63, "clzl one");
_Static_assert(__builtin_clzll(1ULL) == 63, "clzll one");
_Static_assert(__builtin_ctz(8U) == 3, "ctz eight");
_Static_assert(__builtin_ctzl(8UL) == 3, "ctzl eight");
_Static_assert(__builtin_ctzl(0x100UL) == 8, "ctzl 256");
_Static_assert(__builtin_ctzll(0x100ULL) == 8, "ctzll 256");
_Static_assert(__builtin_ffsll(0LL) == 0, "ffsll zero");
_Static_assert(__builtin_ffsll(8LL) == 4, "ffsll eight");
_Static_assert(__builtin_ffsll(-1LL) == 1, "ffsll negative one");
_Static_assert(__builtin_isdigit('7') == 1, "isdigit digit");
_Static_assert(__builtin_isdigit('x') == 0, "isdigit nondigit");

static int runtime_clz(unsigned int value) {
    return __builtin_clz(value);
}

static int runtime_ctzl(unsigned long value) {
    return __builtin_ctzl(value);
}

static int runtime_ctzll(unsigned long long value) {
    return __builtin_ctzll(value);
}

static int runtime_ffsll(long long value) {
    return __builtin_ffsll(value);
}

static int runtime_isdigit(int value) {
    return __builtin_isdigit(value);
}

int main(void) {
    return runtime_clz(1U) == 31 && runtime_ctzl(8UL) == 3 &&
                   runtime_ctzll(0x100ULL) == 8 && runtime_ffsll(8LL) == 4 &&
                   runtime_ffsll(0LL) == 0 && runtime_isdigit('7') && !runtime_isdigit('x')
               ? 0
               : 1;
}
