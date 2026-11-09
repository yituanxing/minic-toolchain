/* Original Linux 6.6.143 sysreg.h encoded ICC_SRE_EL1 sysreg read/write.
 * This is *not* a direct instruction string: GNU .irp/.equ/.macro define
 * the register number used in a fabricated .inst hardware encoding. */

unsigned long minic_gic_sre_read(void) {
    unsigned long value;
    asm volatile(
        "\t.irp\tnum,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30\n"
        "\t.equ\t.L__gpr_num_x\\num, \\num\n"
        "\t.equ\t.L__gpr_num_w\\num, \\num\n"
        "\t.endr\n"
        "\t.equ\t.L__gpr_num_xzr, 31\n"
        "\t.equ\t.L__gpr_num_wzr, 31\n"
        "\t.macro\tmrs_s, rt, sreg\n"
        ".inst (0xd5200000|(\\sreg)|(.L__gpr_num_\\rt))\n"
        "\t.endm\n"
        "\tmrs_s %0, (((3) << 19) | ((0) << 16) | ((12) << 12) | ((12) << 8) | ((5) << 5))\n"
        "\t.purgem\tmrs_s\n"
        : "=r" (value));
    return value;
}

void minic_gic_sre_write(unsigned long value) {
    asm volatile(
        "\t.irp\tnum,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30\n"
        "\t.equ\t.L__gpr_num_x\\num, \\num\n"
        "\t.equ\t.L__gpr_num_w\\num, \\num\n"
        "\t.endr\n"
        "\t.equ\t.L__gpr_num_xzr, 31\n"
        "\t.equ\t.L__gpr_num_wzr, 31\n"
        "\t.macro\tmsr_s, sreg, rt\n"
        ".inst (0xd5000000|(\\sreg)|(.L__gpr_num_\\rt))\n"
        "\t.endm\n"
        "\tmsr_s (((3) << 19) | ((0) << 16) | ((12) << 12) | ((12) << 8) | ((5) << 5)), %x0\n"
        "\t.purgem\tmsr_s\n"
        : : "rZ" (value));
}
