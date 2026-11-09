/* Linux 6.6 x86 MWAIT idle CLFLUSH patch record. Compile-only. */
void linux_idle_clflush_alternative(const void *address) {
    asm volatile(
        "# ALT: oldnstr\n"
        "661:\n\t"
        "\n662:\n"
        "# ALT: padding\n"
        ".skip -(((6651f-6641f)-(662b-661b)) > 0) * "
              "((6651f-6641f)-(662b-661b)),0x90\n"
        "663:\n"
        ".pushsection .altinstructions,\"a\"\n"
        " .long 661b - .\n"
        " .long 6641f - .\n"
        " .4byte (22*32 + (7))\n"
        " .byte 663b-661b\n"
        " .byte 6651f-6641f\n"
        ".popsection\n"
        ".pushsection .altinstr_replacement, \"ax\"\n"
        "# ALT: replacement 1\n"
        "6641:\n\t"
        "clflush (%[addr])\n"
        "6651:\n"
        ".popsection\n"
        : : "i" (0), [addr] "a" (address));
}
