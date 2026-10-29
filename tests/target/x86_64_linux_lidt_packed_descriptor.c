/* Original Linux x86-64 IDT descriptor instruction, privileged execution.
 * This file is for -S and independent GNU AS / ELF inspection only. */
struct desc_ptr {
    unsigned short size;
    unsigned long address;
} __attribute__((packed));

void minic_test_linux_lidt(const struct desc_ptr *descriptor) {
    asm volatile("lidt %0" : : "m" (*descriptor));
}

void minic_test_linux_lgdt(const struct desc_ptr *descriptor) {
    asm volatile("lgdt %0" : : "m" (*descriptor));
}
