/* Linux 6.6 paravirt indirect call: compile/assemble only. The indirect
 * target and metadata are real patching ABI, never executed in userland. */
struct pv_ops {
    struct {
        void (*io_delay)(void);
    } cpu;
};
extern struct pv_ops pv_ops;
register unsigned long current_stack_pointer asm("rsp");

void test_paravirt_io_delay(void) {
    unsigned long __edi = __edi, __esi = __esi, __edx = __edx;
    unsigned long __ecx = __ecx;
    asm volatile("771:\n\t"
        "999:\n\t"
        ".pushsection .discard.retpoline_safe\n\t"
        ".long 999b\n\t"
        ".popsection\n\t"
        "call *%[paravirt_opptr];\n"
        "772:\n"
        ".pushsection .parainstructions,\"a\"\n"
        " .balign 8 \n"
        " .quad 771b\n"
        " .byte %c[paravirt_typenum]\n"
        " .byte 772b-771b\n"
        " .balign 8 \n"
        ".popsection\n"
        : "=D" (__edi), "=S" (__esi), "=d" (__edx), "=c" (__ecx),
          "+r" (current_stack_pointer)
        : [paravirt_typenum] "i" (0),
          [paravirt_opptr] "m" (pv_ops.cpu.io_delay)
        : "memory", "cc", "rax", "r8", "r9", "r10", "r11");
}
