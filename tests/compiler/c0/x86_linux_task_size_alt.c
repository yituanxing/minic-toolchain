/* Linux 6.6.143 TASK_SIZE_MAX: same input roles and complete ELF alternative. */
unsigned long minic_x86_task_size_max(void) {
    unsigned long ret;
    __asm__ volatile (
        "# ALT: oldnstr\n"
        "661:\n\t" "movq %[small],%0" "\n"
        "662:\n"
        "# ALT: padding\n"
        ".skip -(((6651f-6641f)-(662b-661b)) > 0) * "
        "((6651f-6641f)-(662b-661b)),0x90\n"
        "663:\n"
        ".pushsection .altinstructions,\"a\"\n"
        " .long 661b - .\n"
        " .long 6641f - .\n"
        " .4byte (16*32+16)\n"
        " .byte 663b-661b\n"
        " .byte 6651f-6641f\n"
        ".popsection\n"
        ".pushsection .altinstr_replacement, \"ax\"\n"
        "# ALT: replacement 1\n"
        "6641:\n\t" "movq %[large],%0" "\n"
        "6651:\n"
        ".popsection\n"
        : "=r" (ret)
        : "i" (0), [small] "i" ((1UL << 47) - (1UL << 12)),
          [large] "i" ((1UL << 56) - (1UL << 12)));
    return ret;
}
