/* Linux 6.6 clear_page: two alternative CALLs and an RSP +r output.
 * This is a privileged kernel patch-table contract: compile and assemble,
 * then inspect genuine ELF relocation records; never execute the code. */
register unsigned long current_stack_pointer asm("rsp");
void clear_page_orig(void *page);
void clear_page_rep(void *page);
void clear_page_erms(void *page);

#define TEST_ALT_MAX \
    "((6651f-6641f)^(((6651f-6641f)^(6652f-6642f))&-(-((6651f-6641f)<(6652f-6642f)))))"
#define TEST_ALT_SKIP \
    ".skip -( (" TEST_ALT_MAX " - (662b-661b)) > 0) * (" \
    TEST_ALT_MAX " - (662b-661b)), 0x90\n"

void clear_page(void *page) {
    asm volatile(
        "# ALT: oldinstr2\n"
        "661:\n\t"
        "call %c[old]\n"
        "662:\n"
        "# ALT: padding2\n"
        TEST_ALT_SKIP
        "663:\n"
        ".pushsection .altinstructions,\"a\"\n"
        " .long 661b - .\n"
        " .long 6641f - .\n"
        " .4byte ( 3*32+16)\n"
        " .byte 663b-661b\n"
        " .byte 6651f-6641f\n"
        " .long 661b - .\n"
        " .long 6642f - .\n"
        " .4byte ( 9*32+ 9)\n"
        " .byte 663b-661b\n"
        " .byte 6652f-6642f\n"
        ".popsection\n"
        ".pushsection .altinstr_replacement, \"ax\"\n"
        "# ALT: replacement 1\n"
        "6641:\n\t"
        "call %c[new1]\n"
        "6651:\n"
        "# ALT: replacement 2\n"
        "6642:\n\t"
        "call %c[new2]\n"
        "6652:\n"
        ".popsection\n"
        : "=D" (page), "+r" (current_stack_pointer)
        : [old] "i" (clear_page_orig),
          [new1] "i" (clear_page_rep),
          [new2] "i" (clear_page_erms),
          "0" (page)
        : "cc", "memory", "rax", "rcx");
}
