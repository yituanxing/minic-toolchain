typedef __signed__ __int128 signed128_aligned __attribute__((aligned(16)));
typedef unsigned __int128 unsigned128_aligned __attribute__((__aligned__(16)));

struct AlignedPair {
    char lead;
    signed128_aligned value;
};

unsigned long signed128_aligned_size(void) {
    return sizeof(signed128_aligned);
}

unsigned long aligned_pair_size(void) {
    return sizeof(struct AlignedPair);
}
