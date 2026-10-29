/* Exact GNU Linux 6.6.143 read_sysreg() plus write_sysreg_s() macro syntax.
 * KVM debug-sr.h probes ID_AA64DFR0_EL1 and MDCCINT_EL1; Qualcomm Kryo
 * uses S3_3_C15_C0_6 and S3_3_C15_C0_7 through the .irp MSR_S expansion. */
typedef unsigned long long u64;

#define WRITE_SYSREG(name,op0,op1,crn,crm,op2) \
void name(u64 value) { \
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
        "\tmsr_s (((" #op0 ") << 19) | ((" #op1 ") << 16) | ((" #crn ") << 12) | ((" #crm ") << 8) | ((" #op2 ") << 5)), %x0\n" \
        "\t.purgem\tmsr_s\n" : : "rZ"(value)); \
}
WRITE_SYSREG(write_l2cpusrselr_el1,3,3,15,0,6)
WRITE_SYSREG(write_l2cpusrdr_el1,3,3,15,0,7)
WRITE_SYSREG(write_tpidr_el0,3,3,13,0,2)
WRITE_SYSREG(write_tpidrro_el0,3,3,13,0,3)
WRITE_SYSREG(write_tpidr_el1,3,0,13,0,4)
WRITE_SYSREG(write_par_el1,3,0,7,4,0)
WRITE_SYSREG(write_dacr32_el2,3,4,3,0,0)
WRITE_SYSREG(write_ifsr32_el2,3,4,5,0,1)
WRITE_SYSREG(write_dbgvcr32_el2,2,4,0,7,0)

#define READ_HYP(name,register_name) \
u64 name(void) { \
    u64 value; \
    asm volatile("mrs %0, " #register_name : "=r"(value)); \
    return value; \
}
READ_HYP(read_id_aa64dfr0_el1,id_aa64dfr0_el1)
READ_HYP(read_mdccint_el1,mdccint_el1)
READ_HYP(read_tpidrro_el0,tpidrro_el0)
READ_HYP(read_sp_el1,sp_el1)
READ_HYP(read_vmpidr_el2,vmpidr_el2)

/* Linux arch/arm64/include/asm/barrier.h PSB/TSB trace-drain barriers. */
void hyp_trace_drain(void) {
    asm volatile("hint #17" : : : "memory");
    asm volatile("hint #18" : : : "memory");
}
