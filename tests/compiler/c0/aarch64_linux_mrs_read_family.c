/* Original Linux 6.6 AArch64 one-output read_sysreg() GNU ABI contract.
 * Indexed PMU/debug registers must remain physical MRS, never software
 * loads or a compiler barrier.  These functions are EL1/EL2 only. */
#define READ_SYSREG(REG)                                                \
    unsigned long long minic_read_##REG(void) {                          \
        unsigned long long result;                                      \
        asm volatile("mrs %0, " #REG : "=r" (result) : );              \
        return result;                                                  \
    }

READ_SYSREG(pmcr_el0)
READ_SYSREG(pmceid0_el0)
READ_SYSREG(pmceid1_el0)
READ_SYSREG(pmccntr_el0)
READ_SYSREG(pmovsclr_el0)
READ_SYSREG(cntpct_el0)
READ_SYSREG(cnthctl_el2)
READ_SYSREG(SCTLR_EL1)
READ_SYSREG(CPACR_EL1)
READ_SYSREG(cpacr_el1)
READ_SYSREG(actlr_el1)
READ_SYSREG(aidr_el1)
READ_SYSREG(amair_el1)
READ_SYSREG(dbgauthstatus_el1)
READ_SYSREG(elr_el1)
READ_SYSREG(mair_el1)
READ_SYSREG(midr_el1)
READ_SYSREG(revidr_el1)
READ_SYSREG(s3_0_c15_c0_5)
READ_SYSREG(spsr_abt)
READ_SYSREG(ttbr0_el1)

/* Boundaries of the architecturally enumerated debug and PMU banks. */
READ_SYSREG(dbgbcr0_el1)
READ_SYSREG(dbgbcr15_el1)
READ_SYSREG(dbgbvr0_el1)
READ_SYSREG(dbgbvr15_el1)
READ_SYSREG(dbgwcr0_el1)
READ_SYSREG(dbgwvr15_el1)
READ_SYSREG(pmevcntr0_el0)
READ_SYSREG(pmevcntr30_el0)
READ_SYSREG(pmevtyper0_el0)
READ_SYSREG(pmevtyper30_el0)
