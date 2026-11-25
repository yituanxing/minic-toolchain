_Static_assert(__builtin_popcount(0U) == 0, "popcount zero");
_Static_assert(__builtin_popcount(0xffffffffU) == 32, "popcount 32");
_Static_assert(__builtin_popcountl(0UL) == 0, "popcountl zero");
_Static_assert(__builtin_popcountll(~0ULL) == 64, "popcountll 64");
_Static_assert(__builtin_popcountll(0x5555555555555555ULL) == 32,
               "popcountll alternating");

int minic_popcount_u32(unsigned int value) {
    return __builtin_popcount(value);
}

int minic_popcount_ulong(unsigned long value) {
    return __builtin_popcountl(value);
}

int minic_popcount_u64(unsigned long long value) {
    return __builtin_popcountll(value);
}
