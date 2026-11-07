void unsupported(void) {
    __asm__ volatile("nop" : : "Ir"(31));
}
