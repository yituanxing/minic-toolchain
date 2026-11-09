/* Must reject: this record is 16 bytes with natural alignment/padding.
 * The instruction would observe the padding as part of the base address. */
struct desc_ptr {
    unsigned short size;
    unsigned long address;
};

void minic_test_linux_bad_layout(const struct desc_ptr *descriptor) {
    asm volatile("lgdt %0" : : "m" (*descriptor));
}
