struct PointerAligned {
    char byte;
} __attribute__((aligned(sizeof(void *))));

struct OverAligned {
    char byte;
} __attribute__((__aligned__(16)));

struct OverAlignedHolder {
    char lead;
    struct OverAligned payload;
};

unsigned long pointer_aligned_size(void) {
    return sizeof(struct PointerAligned);
}

unsigned long over_aligned_size(void) {
    return sizeof(struct OverAligned);
}

unsigned long over_aligned_holder_size(void) {
    return sizeof(struct OverAlignedHolder);
}

unsigned long over_aligned_holder_offset(void) {
    return __builtin_offsetof(struct OverAlignedHolder, payload);
}
