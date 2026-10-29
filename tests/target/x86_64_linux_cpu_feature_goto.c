/* Linux _static_cpu_has-style asm goto. The frontend must specialize a
 * compile-time feature number, resolve the bit mask and byte offset as Core
 * constants, preserve both GNU C labels, and emit real x86 ELF alternative
 * patch records. The corresponding object may be inspected, not executed:
 * boot_cpu_data and the kernel alternative patcher are external contracts. */
extern unsigned int minic_test_boot_capability[24];

#define MINIC_TEST_ALT_MAX \
    "((6651f-6641f) ^ (((6651f-6641f) ^ (6652f-6642f)) & " \
    "-(-((6651f-6641f) < (6652f-6642f)))))"
#define MINIC_TEST_ALT_SKIP \
    ".skip -( (" MINIC_TEST_ALT_MAX " - (662b-661b)) > 0) * " \
    "(" MINIC_TEST_ALT_MAX " - (662b-661b)), 0x90\n"

static inline __attribute__((always_inline)) int
minic_test_cpu_has(unsigned short feature) {
    __asm__ goto(
        "# ALT: oldinstr2\n"
        "661:\n\t"
        "jmp 6f\n"
        "662:\n"
        "# ALT: padding2\n"
        MINIC_TEST_ALT_SKIP
        "663:\n"
        ".pushsection .altinstructions,\"a\"\n"
        " .long 661b - .\n"
        " .long 6641f - .\n"
        " .4byte (3*32+21)\n"
        " .byte 663b-661b\n"
        " .byte 6651f-6641f\n"
        " .long 661b - .\n"
        " .long 6642f - .\n"
        " .4byte %c[feature]\n"
        " .byte 663b-661b\n"
        " .byte 6652f-6642f\n"
        ".popsection\n"
        ".pushsection .altinstr_replacement, \"ax\"\n"
        "# ALT: replacement 1\n"
        "6641:\n\t"
        "jmp %l[t_no]\n"
        "6651:\n"
        "# ALT: replacement 2\n"
        "6642:\n\t"
        "\n"
        "6652:\n"
        ".popsection\n"
        ".pushsection .altinstr_aux,\"ax\"\n"
        "6:\n"
        " testb %[bitnum],%P[cap_byte] (%% rip)\n"
        " jnz %l[t_yes]\n"
        " jmp %l[t_no]\n"
        ".popsection\n"
        : : [feature] "i" (feature),
            [bitnum] "i" (1 << (feature & 7)),
            [cap_byte] "i" (&((const char *)minic_test_boot_capability)[feature >> 3])
        : : t_yes, t_no);
t_yes:
    return 1;
t_no:
    return 0;
}

int minic_test_cpu_feature_bit_187(void) {
    return minic_test_cpu_has(187);
}

int minic_test_cpu_feature_bit_396(void) {
    return minic_test_cpu_has(396);
}
