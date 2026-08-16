_Static_assert(__builtin_ctzl(8UL) == 3, "ctzl eight");
_Static_assert(__builtin_ctzl(0x100UL) == 8, "ctzl 256");
_Static_assert(__builtin_ffsll(0LL) == 0, "ffsll zero");
_Static_assert(__builtin_ffsll(8LL) == 4, "ffsll eight");
_Static_assert(__builtin_ffsll(-1LL) == 1, "ffsll negative one");
_Static_assert(__builtin_isdigit('7') == 1, "isdigit digit");
_Static_assert(__builtin_isdigit('x') == 0, "isdigit nondigit");

static int runtime_ctzl(unsigned long value) {
    return __builtin_ctzl(value);
}

static int runtime_ffsll(long long value) {
    return __builtin_ffsll(value);
}

static int runtime_isdigit(int value) {
    return __builtin_isdigit(value);
}

int main(void) {
    return runtime_ctzl(8UL) == 3 && runtime_ffsll(8LL) == 4 && runtime_ffsll(0LL) == 0 &&
                   runtime_isdigit('7') && !runtime_isdigit('x')
               ? 0
               : 1;
}
