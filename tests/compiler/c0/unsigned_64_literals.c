typedef unsigned long long MiniU64;

MiniU64 max_unsigned_64(void) {
    return 18446744073709551615ULL;
}

MiniU64 top_unsigned_bit(void) {
    return 18446744073709551615ULL >> 63;
}

int unsigned_max_above_signed_max(void) {
    return 18446744073709551615ULL > 9223372036854775807ULL;
}
