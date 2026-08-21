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

typedef union Int128Words {
    struct {
        unsigned long low;
        unsigned long high;
    } words;
    unsigned128_t full;
} Int128Words;

int int128_pair_equal(const Int128Words *left, const Int128Words *right) {
    unsigned128_t left_value = left->full;
    unsigned128_t right_value = right->full;
    return left_value == right_value;
}

void int128_pair_copy(Int128Words *target, const Int128Words *source) {
    unsigned128_t value = source->full;
    target->full = value;
}

unsigned long int128_mul_shift(unsigned long value, unsigned int scale, unsigned int shift) {
    return (unsigned long)(((unsigned128_t)value * scale) >> shift);
}
