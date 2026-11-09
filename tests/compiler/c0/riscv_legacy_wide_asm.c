static inline unsigned long rv32_irq_save_legacy(void) {
    unsigned long flags = 2;
    __asm__ __volatile__("csrrc %0, 0x100, %1"
                         : "=r"(flags) : "rK"(flags) : "memory");
    return flags;
}
unsigned long rv32_irq_save_entry(void) { return rv32_irq_save_legacy(); }
int rv32_put_user_pair(unsigned int *ptr, unsigned long long value) {
    unsigned long err = 0;
    __asm__ __volatile__("sw %z3, %1\n\tsw %z4, %2"
                         : "+r"(err), "=m"(ptr[0]), "=m"(ptr[1])
                         : "rJ"(value), "rJ"(value >> 32) : "memory");
    return (int)err;
}
