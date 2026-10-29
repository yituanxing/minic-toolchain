/* Linux 6.6.143 ZSTD_DCtx and hibernation LZO thread records can exceed the
 * 16-bit MOVZ immediate encoding. Pointer indexing still uses full sizeof. */
struct LargePointerElement {
    unsigned char payload[65536U + 288U];
};

static struct LargePointerElement values[3];

struct LargePointerElement *large_pointer_add(
    struct LargePointerElement *base, unsigned long index) {
    return base + index;
}

struct LargePointerElement *large_pointer_subtract(
    struct LargePointerElement *base, unsigned long index) {
    return base - index;
}

unsigned long large_pointer_step_bytes(void) {
    struct LargePointerElement *second = large_pointer_add(values, 1UL);
    struct LargePointerElement *first = large_pointer_subtract(second, 1UL);
    return (unsigned long)((unsigned char *)second -
                           (unsigned char *)first);
}

int main(void) {
    return large_pointer_step_bytes() == sizeof(struct LargePointerElement)
        ? 0 : 31;
}
