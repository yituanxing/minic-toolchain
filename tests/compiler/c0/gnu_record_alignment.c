struct PointerAligned {
    char byte;
} __attribute__((aligned(sizeof(void *))));

struct __attribute__((aligned(16))) PrefixAligned {
    char byte;
};

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

unsigned long prefix_aligned_size(void) {
    return sizeof(struct PrefixAligned);
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

struct DesignatedOnly {
    int first;
    int second;
} __attribute__((__designated_init__));

struct DesignatedAligned {
    char byte;
} __attribute__((__designated_init__, aligned(16)));

unsigned long designated_only_size(void) {
    return sizeof(struct DesignatedOnly);
}

unsigned long designated_aligned_size(void) {
    return sizeof(struct DesignatedAligned);
}
