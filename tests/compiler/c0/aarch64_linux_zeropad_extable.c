/* Exact Linux 6.6 arm64 word-at-a-time.h fault-fixup ASM, not a plain load.
 * GNU's original expression builds a 12-byte __ex_table entry with PREL32
 * fault/fixup PCs and data=4, (result-register | address-register << 5).
 * Keep the entire original .irp/.equ + GNU operand-index expression here:
 * this is an independent source qualification and assembler oracle. */
unsigned long minic_linux_zeropad(const void *addr) {
    unsigned long ret;
    asm(
        "1:\tldr\t%0, %2\n"
        "2:\n"
        "\t.irp\tnum,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30\n"
        "\t.equ\t.L__gpr_num_x\\num, \\num\n"
        "\t.equ\t.L__gpr_num_w\\num, \\num\n"
        "\t.endr\n"
        "\t.equ\t.L__gpr_num_xzr, 31\n"
        "\t.equ\t.L__gpr_num_wzr, 31\n"
        ".pushsection\t__ex_table, \"a\"\n"
        ".align\t\t2\n"
        ".long\t\t((1b) - .)\n"
        ".long\t\t((2b) - .)\n"
        ".short\t\t(4)\n"
        ".short\t\t((((.L__gpr_num_%0) << 0) | ((.L__gpr_num_%1) << 5)))\n"
        ".popsection\n"
        : "=&r" (ret)
        : "r" (addr), "Q" (*(unsigned long *)addr));
    return ret;
}

/* A second independent call verifies that output/register staging cannot
 * accidentally overwrite the word or mis-handle the original pointer. */
unsigned long minic_linux_zeropad_twice(const void *addr) {
    return minic_linux_zeropad(addr) ^
           minic_linux_zeropad((const unsigned long *)addr + 1);
}
