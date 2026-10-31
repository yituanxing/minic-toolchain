/* The x86 Linux runtime rewrites paravirt inline-asm sites with a 5-byte
 * CALL rel32. The emitted 771..772 span must therefore be at least 5 bytes
 * even if MiniC selects a 3-byte register-indirect memory CALL. */
struct pv_ops_type {
    struct { void (*safe_halt)(void); } irq;
};

static int callback_count;
static void callback(void) { ++callback_count; }
struct pv_ops_type pv_ops = { .irq = { .safe_halt = callback } };
register unsigned long current_stack_pointer __asm__("rsp");

void pv_call(void) {
    unsigned long edi = 0, esi = 0, edx = 0, ecx = 0;
    asm volatile(
        "771:\n\t"
        "999:\n\t"
        ".pushsection .discard.retpoline_safe\n\t"
        ".long 999b\n\t"
        ".popsection\n\t"
        "call *%[paravirt_opptr];\n"
        "772:\n"
        ".pushsection .parainstructions,\"a\"\n"
        " .balign 8\n"
        " .quad 771b\n"
        " .byte %c[paravirt_typenum]\n"
        " .byte 772b-771b\n"
        " .balign 8\n"
        ".popsection\n"
        : "=D" (edi), "=S" (esi), "=d" (edx), "=c" (ecx),
          "+r" (current_stack_pointer)
        : [paravirt_typenum] "i" (1),
          [paravirt_opptr] "m" (pv_ops.irq.safe_halt)
        : "memory", "cc", "rax", "r8", "r9", "r10", "r11");
}

int main(void) {
    pv_call();
    return callback_count == 1 ? 42 : 1;
}
