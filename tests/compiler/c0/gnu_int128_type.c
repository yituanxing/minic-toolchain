typedef __signed__ __int128 signed128_t;
typedef unsigned __int128 unsigned128_t;
typedef __uint128 direct_unsigned128_t;

struct Int128Layout {
    char lead;
    signed128_t value;
};

unsigned long signed128_size(void) {
    return sizeof(signed128_t);
}

unsigned long unsigned128_size(void) {
    return sizeof(unsigned128_t);
}

unsigned long direct_unsigned128_size(void) {
    return sizeof(direct_unsigned128_t);
}

unsigned long int128_record_size(void) {
    return sizeof(struct Int128Layout);
}
