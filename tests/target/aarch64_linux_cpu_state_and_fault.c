/* Linux 6.6 arm64 per-CPU TPIDR, stage-1 translation, and erratum 1508412.
 * The kernel applies .altinstructions at boot; no runtime tests here. */
void linux_set_my_cpu_offset(unsigned long offset) {
    asm volatile(
        ".if 1 == 1\n"
        "661:\n\t"
        "msr tpidr_el1, %0\n"
        "662:\n"
        ".pushsection .altinstructions,\"a\"\n"
        " .word 661b - .\n"
        " .word 663f - .\n"
        " .hword 47\n"
        " .byte 662b-661b\n"
        " .byte 664f-663f\n"
        ".popsection\n"
        ".subsection 1\n"
        "663:\n\t"
        "msr tpidr_el2, %0\n"
        "664:\n\t"
        ".org . - (664b-663b) + (662b-661b)\n\t"
        ".org . - (662b-661b) + (664b-663b)\n\t"
        ".previous\n"
        ".endif\n"
        : : "r" (offset) : "memory");
}

void linux_probe_el1_translation(unsigned long address) {
    asm volatile("at s1e1r, %0" : : "r" (address));
    asm volatile("isb" : : : "memory");
    asm(
        ".if 1 == 1\n"
        "661:\n\t"
        "nop\n"
        "662:\n"
        ".pushsection .altinstructions,\"a\"\n"
        " .word 661b - .\n"
        " .word 663f - .\n"
        " .hword 71\n"
        " .byte 662b-661b\n"
        " .byte 664f-663f\n"
        ".popsection\n"
        ".subsection 1\n"
        "663:\n\t"
        "dmb sy\n"
        "664:\n\t"
        ".org . - (664b-663b) + (662b-661b)\n\t"
        ".org . - (662b-661b) + (664b-663b)\n\t"
        ".previous\n"
        ".endif\n");
}
