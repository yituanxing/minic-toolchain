/* Linux 6.6.143 hw_breakpoint.h AARCH64_DBG_READ and arm_pmuv3.h
 * RETURN_READ_PMEVCNTRN/PMEVN_SWITCH use exactly this GNU read_sysreg
 * expression and =r operand contract. The real kernel calls at EL1. */
typedef unsigned long long u64;
#define READ_SYSREG(reg) \
    static u64 read_##reg(void) { \
        u64 value; \
        __asm__ __volatile__("mrs %0, " #reg : "=r"(value)); \
        return value; \
    }

#define DEBUG_BANK(n) \
    READ_SYSREG(dbgbcr##n##_el1) \
    READ_SYSREG(dbgbvr##n##_el1) \
    READ_SYSREG(dbgwcr##n##_el1) \
    READ_SYSREG(dbgwvr##n##_el1)
DEBUG_BANK(0)
DEBUG_BANK(1)
DEBUG_BANK(2)
DEBUG_BANK(3)
DEBUG_BANK(4)
DEBUG_BANK(5)
DEBUG_BANK(6)
DEBUG_BANK(7)
DEBUG_BANK(8)
DEBUG_BANK(9)
DEBUG_BANK(10)
DEBUG_BANK(11)
DEBUG_BANK(12)
DEBUG_BANK(13)
DEBUG_BANK(14)
DEBUG_BANK(15)

/* Representative PMEVN_SWITCH endpoints plus all named PMUv3 reads. */
READ_SYSREG(pmevcntr0_el0)
READ_SYSREG(pmevcntr15_el0)
READ_SYSREG(pmevcntr30_el0)
READ_SYSREG(pmcr_el0)
READ_SYSREG(pmccntr_el0)
READ_SYSREG(pmovsclr_el0)
READ_SYSREG(pmceid0_el0)
READ_SYSREG(pmceid1_el0)

#define DEBUG_READ_SUM(n) \
    read_dbgbcr##n##_el1() + read_dbgbvr##n##_el1() + \
    read_dbgwcr##n##_el1() + read_dbgwvr##n##_el1()
u64 all_debug_pmu_reads(void) {
    return DEBUG_READ_SUM(0) + DEBUG_READ_SUM(1) +
           DEBUG_READ_SUM(2) + DEBUG_READ_SUM(3) +
           DEBUG_READ_SUM(4) + DEBUG_READ_SUM(5) +
           DEBUG_READ_SUM(6) + DEBUG_READ_SUM(7) +
           DEBUG_READ_SUM(8) + DEBUG_READ_SUM(9) +
           DEBUG_READ_SUM(10) + DEBUG_READ_SUM(11) +
           DEBUG_READ_SUM(12) + DEBUG_READ_SUM(13) +
           DEBUG_READ_SUM(14) + DEBUG_READ_SUM(15) +
           read_pmevcntr0_el0() + read_pmevcntr15_el0() +
           read_pmevcntr30_el0() + read_pmcr_el0() +
           read_pmccntr_el0() + read_pmovsclr_el0() +
           read_pmceid0_el0() + read_pmceid1_el0();
}
