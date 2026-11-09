/* Compressed x86 cpuflags: constant/variable bit forms, CPUID ties, and
 * RFLAGS ID-bit probe. Compile only: do not execute CPU feature probes. */
_Bool linux_boot_test_bit(int number, const unsigned int *memory) {
    _Bool flag;
    asm("btl %2,%1" "\n\t/* output condition code " "c" "*/\n"
        : "=@cc" "c" (flag) : "m" (*memory), "Ir" (number));
    return flag;
}

void linux_boot_set_bit(int number, unsigned int *memory) {
    asm("btsl %1,%0" : "+m" (*memory) : "Ir" (number));
}

void linux_boot_cpuid(unsigned int leaf, unsigned int subleaf,
    unsigned int *a, unsigned int *b, unsigned int *c, unsigned int *d) {
    asm volatile("cpuid" : "=a" (*a), "=b" (*b), "=c" (*c), "=d" (*d)
        : "0" (leaf), "2" (subleaf));
}

int linux_boot_has_eflag(unsigned long mask) {
    unsigned long before, after;
    asm volatile("pushfq" "\t\n\t"
        "pushfq" "\t\n\t"
        "pop %0\t\n\t"
        "mov %0,%1\t\n\t"
        "xor %2,%1\t\n\t"
        "push %1\t\n\t"
        "popfq" "\t\n\t"
        "pushfq" "\t\n\t"
        "pop %1\t\n\t"
        "popfq"
        : "=&r" (before), "=&r" (after) : "ri" (mask));
    return !!((before ^ after) & mask);
}
