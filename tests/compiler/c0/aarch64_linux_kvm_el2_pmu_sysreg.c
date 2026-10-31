/* Exact Linux 6.6.143 sysreg.h style: one volatile 64-bit rZ input.
 * No privileged instructions are executed by this compile-only probe. */
#define WRITE_SYSREG(reg) \
    void write_sysreg_##reg(unsigned long long value) { \
        asm volatile("msr " #reg ", %x0" : : "rZ" (value)); \
    }

WRITE_SYSREG(par_el1)
WRITE_SYSREG(elr_el2)
WRITE_SYSREG(spsr_el2)
WRITE_SYSREG(spsr_abt)
WRITE_SYSREG(spsr_und)
WRITE_SYSREG(vmpidr_el2)
WRITE_SYSREG(cnthctl_el2)
WRITE_SYSREG(cntvoff_el2)
WRITE_SYSREG(pmuserenr_el0)
WRITE_SYSREG(pmcntenset_el0)
WRITE_SYSREG(pmcntenclr_el0)
WRITE_SYSREG(pmovsclr_el0)
WRITE_SYSREG(pmevtyper0_el0)
WRITE_SYSREG(pmevcntr0_el0)
WRITE_SYSREG(pmselr_el0)
WRITE_SYSREG(pmintenset_el1)
WRITE_SYSREG(pmintenclr_el1)
WRITE_SYSREG(pmccntr_el0)
WRITE_SYSREG(pmccfiltr_el0)
WRITE_SYSREG(pmcr_el0)
WRITE_SYSREG(dbgbcr15_el1)
WRITE_SYSREG(dbgbvr0_el1)
WRITE_SYSREG(CPACR_EL1)
