/* Must reject: the CPU operand is 10 bytes (16-bit limit + 64-bit base).
 * A 6-byte object is not a valid x86-64 descriptor table operand. */
struct desc_ptr {
    unsigned short size;
    unsigned int address;
} __attribute__((packed));

void minic_test_linux_bad_size(const struct desc_ptr *descriptor) {
    asm volatile("lidt %0" : : "m" (*descriptor));
}
