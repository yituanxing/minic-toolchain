unsigned long sbi_eight_operand_shape(unsigned long v0,
                                      unsigned long v1,
                                      unsigned long v2,
                                      unsigned long v3,
                                      unsigned long v4,
                                      unsigned long v5,
                                      unsigned long v6,
                                      unsigned long v7) {
    register unsigned long a0 asm("a0") = v0;
    register unsigned long a1 asm("a1") = v1;
    register unsigned long a2 asm("a2") = v2;
    register unsigned long a3 asm("a3") = v3;
    register unsigned long a4 asm("a4") = v4;
    register unsigned long a5 asm("a5") = v5;
    register unsigned long a6 asm("a6") = v6;
    register unsigned long a7 asm("a7") = v7;

    asm volatile("add %0, %0, %2\n\t# sbi %1 %3 %4 %5 %6 %7"
                 : "+r"(a0), "+r"(a1)
                 : "r"(a2), "r"(a3), "r"(a4), "r"(a5), "r"(a6), "r"(a7)
                 : "memory");
    return a0 + a1;
}
