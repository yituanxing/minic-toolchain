/* Linux ALTERNATIVE_WRMSR: native two-byte NOP baseline plus feature-gated
 * WRMSR replacement. This fixture must assemble with GNU AS but must never
 * execute in user mode (WRMSR is privileged). */
void minic_linux_feature_msr_write(unsigned int msr, unsigned long val) {
    __asm__ volatile(
        "# ALT: oldnstr\n"
        "661:\n\t\n"
        "662:\n"
        "# ALT: padding\n"
        ".skip -(((6651f-6641f)-(662b-661b))>0) * "
              "((6651f-6641f)-(662b-661b)),0x90\n"
        "663:\n"
        ".pushsection .altinstructions,\"a\"\n"
        " .long 661b - .\n"
        " .long 6641f - .\n"
        " .4byte %c[feature]\n"
        " .byte 663b-661b\n"
        " .byte 6651f-6641f\n"
        ".popsection\n"
        ".pushsection .altinstr_replacement, \"ax\"\n"
        "# ALT: replacement 1\n"
        "6641:\n\twrmsr\n"
        "6651:\n"
        ".popsection\n"
        : : "c"(msr), "a"((unsigned int)val),
            "d"((unsigned int)(val >> 32)), [feature] "i"(180)
        : "memory");
}
