/* Type-size-only test: a 64-bit LP64 pointer-element stride needs MOVK at
 * bit 32. No object of this size is allocated at runtime. */
struct VastPointerElement {
    unsigned char payload[4295033120ULL];
};

struct VastPointerElement *vast_pointer_add(
    struct VastPointerElement *base, unsigned long index) {
    return base + index;
}

unsigned long vast_pointer_element_size(void) {
    return sizeof(struct VastPointerElement);
}
