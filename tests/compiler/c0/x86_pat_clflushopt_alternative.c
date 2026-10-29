/* Original Linux 6.6.143 arch/x86/include/asm/special_insns.h ALT owner:
 * selector 9*32+23, actual cache flush and 66-prefix CLFLUSHOPT replacement. */
static void clflushopt(volatile void *p)
{
    asm volatile (
        "# ALT: oldnstr\n" "661:\n\t"
        ".byte 0x3e; clflush %P0"
        "\n662:\n" "# ALT: padding\n"
        ".skip -(((6651f-6641f)-(662b-661b)) > 0) * "
        "((6651f-6641f)-(662b-661b)),0x90\n"
        "663:\n" ".pushsection .altinstructions,\"a\"\n"
        " .long 661b - .\n" " .long 6641f - .\n"
        " .4byte (9*32+23)\n"
        " .byte 663b-661b\n" " .byte 6651f-6641f\n"
        ".popsection\n"
        ".pushsection .altinstr_replacement, \"ax\"\n"
        "# ALT: replacement 1\n" "6641:\n\t"
        ".byte 0x66; clflush %P0"
        "\n6651:\n" ".popsection\n"
        : "+m" (*(volatile char *)p) : "i" (0));
}

int main(void)
{
    char cacheline[128] = { 0 };
    cacheline[13] = 42;
    clflushopt(&cacheline[13]);
    return cacheline[13];
}
