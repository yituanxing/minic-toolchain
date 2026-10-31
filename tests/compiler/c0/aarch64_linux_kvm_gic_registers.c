/* Linux 6.6.143 arch/arm64/include/asm/sysreg.h read/write_gicreg.
 * Arm GICv3 LR0..15 encode CRm=12/13, AP0R/AP1R encode CRm=8/9. */
#define WRITE_GIC_REG(name, crm, op2) \
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
        "\tmsr_s (((3) << 19) | ((4) << 16) | ((12) << 12) | ((" #crm ") << 8) | ((" #op2 ") << 5)), %x0\n" \
        "\t.purgem\tmsr_s\n" \
        : : "rZ" (value)); \
}

WRITE_GIC_REG(ICH_AP0R0_EL2, 8, 0)
WRITE_GIC_REG(ICH_AP0R1_EL2, 8, 1)
WRITE_GIC_REG(ICH_AP0R2_EL2, 8, 2)
WRITE_GIC_REG(ICH_AP0R3_EL2, 8, 3)
WRITE_GIC_REG(ICH_AP1R0_EL2, 9, 0)
WRITE_GIC_REG(ICH_AP1R1_EL2, 9, 1)
WRITE_GIC_REG(ICH_AP1R2_EL2, 9, 2)
WRITE_GIC_REG(ICH_AP1R3_EL2, 9, 3)
WRITE_GIC_REG(ICH_LR0_EL2, 12, 0)
WRITE_GIC_REG(ICH_LR1_EL2, 12, 1)
WRITE_GIC_REG(ICH_LR2_EL2, 12, 2)
WRITE_GIC_REG(ICH_LR3_EL2, 12, 3)
WRITE_GIC_REG(ICH_LR4_EL2, 12, 4)
WRITE_GIC_REG(ICH_LR5_EL2, 12, 5)
WRITE_GIC_REG(ICH_LR6_EL2, 12, 6)
WRITE_GIC_REG(ICH_LR7_EL2, 12, 7)
WRITE_GIC_REG(ICH_LR8_EL2, 13, 0)
WRITE_GIC_REG(ICH_LR9_EL2, 13, 1)
WRITE_GIC_REG(ICH_LR10_EL2, 13, 2)
WRITE_GIC_REG(ICH_LR11_EL2, 13, 3)
WRITE_GIC_REG(ICH_LR12_EL2, 13, 4)
WRITE_GIC_REG(ICH_LR13_EL2, 13, 5)
WRITE_GIC_REG(ICH_LR14_EL2, 13, 6)
WRITE_GIC_REG(ICH_LR15_EL2, 13, 7)
