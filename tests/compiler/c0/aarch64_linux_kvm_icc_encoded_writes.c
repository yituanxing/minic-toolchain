/* Exact sysreg.h write_sysreg_s macro skeleton, distinct ICC AP0R and
 * AP1R encodings checked against Linux sysreg.h CRm/op2 constants. */
#define ICC_WRITE(name, crn, crm, op2) \
void write_##name(unsigned long long value) { \
    asm volatile( \
        "\t.irp\tnum,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30\n" \
        "\t.equ\t.L__gpr_num_x\\num, \\num\n" \
        "\t.equ\t.L__gpr_num_w\\num, \\num\n" \
        "\t.endr\n" \
        "\t.equ\t.L__gpr_num_xzr, 31\n" \
        "\t.equ\t.L__gpr_num_wzr, 31\n" \
        "\t.macro\tmsr_s, sreg, rt\n" \
        ".inst (0xd5000000|(\\sreg)|(.L__gpr_num_\\rt))\n" \
        "\t.endm\n" \
        "\tmsr_s (((3) << 19) | ((0) << 16) | ((" #crn ") << 12) | ((" #crm ") << 8) | ((" #op2 ") << 5)), %x0\n" \
        "\t.purgem\tmsr_s\n" : : "rZ"(value)); \
}
ICC_WRITE(ICC_AP0R0_EL1, 12, 8, 4|0)
ICC_WRITE(ICC_AP0R1_EL1, 12, 8, 4|1)
ICC_WRITE(ICC_AP0R2_EL1, 12, 8, 4|2)
ICC_WRITE(ICC_AP0R3_EL1, 12, 8, 4|3)
ICC_WRITE(ICC_AP1R0_EL1, 12, 9, 0)
ICC_WRITE(ICC_AP1R1_EL1, 12, 9, 1)
ICC_WRITE(ICC_AP1R2_EL1, 12, 9, 2)
ICC_WRITE(ICC_AP1R3_EL1, 12, 9, 3)
