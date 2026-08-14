typedef unsigned long long MiniU64;

_Static_assert(sizeof(2147483648) == sizeof(long),
               "decimal literal should select long after int overflow");
_Static_assert((0xffffffff >> 31) == 1,
               "hex literal should select unsigned int when signed int cannot represent it");
_Static_assert(sizeof(0x8000000000000000) == sizeof(unsigned long),
               "large hex literal should select unsigned long on RV64");

MiniU64 max_unsigned_64(void) {
    return 18446744073709551615ULL;
}

MiniU64 top_unsigned_bit(void) {
    return 18446744073709551615ULL >> 63;
}

int unsigned_max_above_signed_max(void) {
    return 18446744073709551615ULL > 9223372036854775807ULL;
}
