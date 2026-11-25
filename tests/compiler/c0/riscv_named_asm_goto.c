/* Exact GNU C asm-goto source ownership: named `i` input and C label.
 * C ingress binds names once; RV32/RV64 typed MachineIR preserves the source
 * and proves the real Core CFG branch and physical fallthrough. */
int rv32_jump_owner(int test) {
    if (test)
        __asm__ __volatile__ goto (
            ".option push\n\t.option norelax\n"
            "1: nop\n\t"
            ".pushsection __jump_table,\"aw\"\n\t"
            ".long 1b - ., %l[enabled] - .\n\t"
            ".word %[key]\n\t"
            ".popsection\n\t.option pop"
            : : [key] "i"(11) : : enabled);
    return 0;
enabled:
    return 1;
}

/* Two distinct actual labels must retain two independently verifiable
 * Machine successors. The first three GNU operands are constant facts. */
int rv32_jump_two_labels(int choice) {
    if (choice)
        __asm__ __volatile__ goto (
            ".pushsection __jump_table,\"aw\"\n\t"
            ".word %[alpha], %[beta], %[gamma]\n\t"
            ".long %l[yes] - ., %l[no] - .\n\t"
            ".popsection"
            : : [alpha] "i"(3), [beta] "i"(5), [gamma] "i"(7)
            : : yes, no);
    return 0;
yes:
    return 1;
no:
    return 2;
}
