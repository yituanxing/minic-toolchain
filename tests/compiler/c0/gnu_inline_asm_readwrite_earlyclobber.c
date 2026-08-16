static unsigned long readwrite_earlyclobber(unsigned long left, unsigned long right) {
    register unsigned long a0 asm("a0") = left;
    register unsigned long a1 asm("a1") = right;
    unsigned long scratch = 3;
    unsigned long extra = 7;

    asm volatile("add %0, %0, %3\n\t"
                 "add %1, %1, %3\n\t"
                 "add %2, %2, %3"
                 : "+&r"(a0), "+&r"(a1), "+&r"(scratch)
                 : "r"(extra)
                 : "memory");
    return a0 + a1 + scratch;
}

int main(void) {
    return readwrite_earlyclobber(4, 5) == 33 ? 0 : 1;
}
