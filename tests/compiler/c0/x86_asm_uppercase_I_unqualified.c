void unsupported(void) {
    __asm__ volatile("nop" : : "I"(31));
}
