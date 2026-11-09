/* Original Loong Linux native INVTLB / LL-SC add-unless ASM forms.
 * These source templates exercise typed form variants without executing
 * privileged TLB operations on the host. */
void invtlb_asid_va(unsigned int asid, unsigned long va) {
    __asm__ __volatile__("invtlb %0, %1, %2" : :
        "i"(0), "r"(asid), "r"(va) : "memory");
}

void invtlb_va(unsigned long va) {
    __asm__ __volatile__("invtlb %0, $zero, %1" : :
        "i"(0), "r"(va) : "memory");
}

void invtlb_asid(unsigned int asid) {
    __asm__ __volatile__("invtlb %0, %1, $zero" : :
        "i"(0), "r"(asid) : "memory");
}

void invtlb_all(void) {
    __asm__ __volatile__("invtlb %0, $zero, $zero" : : "i"(0) : "memory");
}

unsigned int add_unless_word(volatile unsigned int *ptr,
                             unsigned int add, unsigned int unless) {
    unsigned int prior, sc;
    __asm__ __volatile__(
        "0: ll.w %[p], %[c]\n"
        "beq %[p], %[u], 1f\n"
        "add.w %[rc], %[p], %[a]\n"
        "sc.w %[rc], %[c]\n"
        "beqz %[rc], 0b\n"
        "b 2f\n"
        "1: dbar 0x700\n"
        "2:"
        : [p] "=&r"(prior), [rc] "=&r"(sc), [c] "=ZB"(*ptr)
        : [a] "r"(add), [u] "r"(unless) : "memory");
    return prior;
}

unsigned long add_unless_double(volatile unsigned long *ptr,
                                 unsigned long add, unsigned long unless) {
    unsigned long prior, sc;
    __asm__ __volatile__(
        "0: ll.d %[p], %[c]\n"
        "beq %[p], %[u], 1f\n"
        "add.d %[rc], %[p], %[a]\n"
        "sc.d %[rc], %[c]\n"
        "beqz %[rc], 0b\n"
        "b 2f\n"
        "1: dbar 0x700\n"
        "2:"
        : [p] "=&r"(prior), [rc] "=&r"(sc), [c] "=ZB"(*ptr)
        : [a] "r"(add), [u] "r"(unless) : "memory");
    return prior;
}
