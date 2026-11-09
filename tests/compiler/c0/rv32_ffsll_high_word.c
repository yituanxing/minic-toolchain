int ffsll_high_word(long long x) { return __builtin_ffsll(x); }

int main(void) {
    return ffsll_high_word(0) == 0 &&
           ffsll_high_word(1LL) == 1 &&
           ffsll_high_word(1LL << 40) == 41 &&
           ffsll_high_word(1LL << 63) == 64 ? 0 : 1;
}
