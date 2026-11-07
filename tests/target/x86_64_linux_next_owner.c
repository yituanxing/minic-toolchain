/* Linux 6.6 original failure-owner forms. Privileged instructions are
 * compile/assemble-only: the test never executes either far jumps or fences. */
struct real_mode_header {
    unsigned int machine_real_restart_asm;
};

void linux_real_mode_restart(struct real_mode_header *header,
    unsigned int restart_type) {
    asm volatile("ljmpl *%0" : :
        "m" (header->machine_real_restart_asm), "D" (restart_type));
    asm volatile("375:\n\t"
        ".pushsection .discard.unreachable\n\t"
        ".long 375b - .\n\t"
        ".popsection\n\t" : : "i" (375));
}

void linux_weak_wrmsr_fence(void) {
    asm volatile(
        "# ALT: oldnstr\n"
        "661:\n\tmfence; lfence\n"
        "662:\n# ALT: padding\n"
        ".skip -(((6651f-6641f)-(662b-661b)) > 0) * "
        "((6651f-6641f)-(662b-661b)),0x90\n"
        "663:\n"
        ".pushsection .altinstructions,\"a\"\n"
        " .long 661b - .\n"
        " .long 6641f - .\n"
        " .4byte (((1 << 0) << 16) | ((11*32+27)))\n"
        " .byte 663b-661b\n"
        " .byte 6651f-6641f\n"
        ".popsection\n"
        ".pushsection .altinstr_replacement, \"ax\"\n"
        "# ALT: replacement 1\n"
        "6641:\n\t\n6651:\n"
        ".popsection\n" : : : "memory");
}
