/* Linux's original type-20 unaligned word load. The exception handler is
 * kernel-only; userspace runtime tests exercise only mapped memory. */
unsigned long linux_load_unaligned_zeropad(const void *addr) {
    unsigned long result;
    asm volatile(
        "1:\tmov %[mem], %[ret]\n"
        "2:\n"
        " .pushsection \"__ex_table\",\"a\"\n"
        " .balign 4\n"
        " .long (" "1b" ") - .\n"
        " .long (" "2b" ") - .\n"
        " .long " "20" " \n"
        " .popsection\n"
        : [ret] "=r"(result)
        : [mem] "m"(*(unsigned long *)addr));
    return result;
}
