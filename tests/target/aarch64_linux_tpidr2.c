/* Actual sysreg.h write_sysreg_s() GNU macro for SME TPIDR2_EL0. */
void linux_clear_tpidr2_el0(void) {
    unsigned long long value = 0;
    asm volatile(
        "\t.irp\tnum,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30\n"
        "\t.equ\t.L__gpr_num_x\\num, \\num\n"
        "\t.equ\t.L__gpr_num_w\\num, \\num\n"
        "\t.endr\n"
        "\t.equ\t.L__gpr_num_xzr, 31\n"
        "\t.equ\t.L__gpr_num_wzr, 31\n"
        "\t.macro\tmsr_s, sreg, rt\n"
        ".inst " "(0xd5000000|(\\sreg)|(.L__gpr_num_\\rt))" "\n\t"
        "\t.endm\n"
        "\tmsr_s "
        "(((3) << 19) | ((3) << 16) | ((13) << 12) | ((0) << 8) | ((5) << 5))"
        ", " "%x0" "\n"
        "\t.purgem\tmsr_s\n"
        : : "rZ"(value));
}
