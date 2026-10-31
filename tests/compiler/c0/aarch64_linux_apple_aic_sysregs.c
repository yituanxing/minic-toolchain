/* Linux 6.6 drivers/irqchip/irq-apple-aic.c implementation-defined SYS_*
 * encodings passed through the *complete* arm64 sysreg.h GNU MSR_S helper.
 * Never execute EL1/EL2 Apple system-register instructions in user mode. */
#define WRITE_AIC_REG(NAME, OP1, CRN, CRM, OP2)                       \
void minic_apple_write_##NAME(unsigned long long value) {             \
    asm volatile(                                                       \
        "\t.irp\tnum,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30\n" \
        "\t.equ\t.L__gpr_num_x\\num, \\num\n"                      \
        "\t.equ\t.L__gpr_num_w\\num, \\num\n"                      \
        "\t.endr\n"                                                   \
        "\t.equ\t.L__gpr_num_xzr, 31\n"                              \
        "\t.equ\t.L__gpr_num_wzr, 31\n"                              \
        "\t.macro\tmsr_s, sreg, rt\n"                               \
        ".inst (0xd5000000|(\\sreg)|(.L__gpr_num_\\rt))\n"          \
        "\t.endm\n"                                                   \
        "\tmsr_s (((3) << 19) | ((" #OP1 ") << 16) | ((" #CRN ") << 12) | ((" #CRM ") << 8) | ((" #OP2 ") << 5)), %x0\n" \
        "\t.purgem\tmsr_s\n"                                         \
        : : "rZ" (value));                                               \
}

WRITE_AIC_REG(ipi_rr_local,   5, 15, 0, 0)
WRITE_AIC_REG(ipi_rr_global,  5, 15, 0, 1)
WRITE_AIC_REG(ipi_sr,         5, 15, 1, 1)
WRITE_AIC_REG(vm_tmr_fiq_ena, 5, 15, 1, 3)
WRITE_AIC_REG(pmcr0,          1, 15, 0, 0)
WRITE_AIC_REG(upmcr0,         7, 15, 0, 4)
