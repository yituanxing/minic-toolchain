struct LargePointerElement {
    unsigned char payload[65536U + 288U];
};

/* A non-integral pointer subscript is invalid GNU C, not a target offset. */
struct LargePointerElement *large_pointer_bad_index(
    struct LargePointerElement *base, float index) {
    return base + index;
}
