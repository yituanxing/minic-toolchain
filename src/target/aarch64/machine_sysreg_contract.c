#include "target/aarch64/asm_contract_internal.h"
/* All finite source-ISA system register grammars live here, independently
 * linkable by typed MachineIR selectors and source-language admission. */

/* Exact original Linux 6.6.143 MRS register read family. These privileged
 * system registers can be assembled for EL1; do not user-mode QEMU execute
 * them and never turn this into arbitrary template passthrough. */
#define AARCH64_LINUX_DEBUG_READ_BANK(n) \
    {"mrs%0,dbgbvr" #n "_el1", "dbgbvr" #n "_el1"}, \
    {"mrs%0,dbgbcr" #n "_el1", "dbgbcr" #n "_el1"}, \
    {"mrs%0,dbgwvr" #n "_el1", "dbgwvr" #n "_el1"}, \
    {"mrs%0,dbgwcr" #n "_el1", "dbgwcr" #n "_el1"}
#define AARCH64_LINUX_PMU_READ_BANK(n) \
    {"mrs%0,pmevcntr" #n "_el0", "pmevcntr" #n "_el0"}, \
    {"mrs%0,pmevtyper" #n "_el0", "pmevtyper" #n "_el0"}
const char *aarch64_linux_mrs_register(const char *text, size_t len) {
    char normalized[96];
    size_t i, used = 0U;
    static const struct { const char *text; const char *reg; } allowed[] = {
        {"mrs%0,sp_el0", "sp_el0"},
        {"mrs%0,tpidr_el1", "tpidr_el1"},
        {"mrs%0,tpidr_el2", "tpidr_el2"},
        {"mrs%0,clidr_el1", "clidr_el1"},
        {"mrs%0,cntkctl_el1", "cntkctl_el1"},
        {"mrs%0,cntp_ctl_el0", "cntp_ctl_el0"},
        {"mrs%0,cntv_ctl_el0", "cntv_ctl_el0"},
        {"mrs%0,cntfrq_el0", "cntfrq_el0"},
        {"mrs%0,cntpct_el0", "cntpct_el0"},
        {"mrs%0,cntvct_el0", "cntvct_el0"},
        {"mrs%0,CNTVCT_EL0", "CNTVCT_EL0"}, /* Linux MTE timer read */
        {"mrs%0,vbar_el1", "vbar_el1"}, /* SDEI exception vector */
        /* Linux arm_pmuv3.h read_pmcr/read_pmccntr/read_pmovsclr,
         * read_pmceid0/1, and PMEVN_SWITCH-expanded read_pmevcntrn /
         * read_pmevtypern.
         * These are architecturally finite registers; the C source
         * constrains the output to GNU "=r" 64-bit GPR. */
        {"mrs%0,pmcr_el0", "pmcr_el0"},
        {"mrs%0,pmccntr_el0", "pmccntr_el0"},
        {"mrs%0,pmccfiltr_el0", "pmccfiltr_el0"},
        {"mrs%0,pmovsclr_el0", "pmovsclr_el0"},
        {"mrs%0,pmceid0_el0", "pmceid0_el0"},
        {"mrs%0,pmceid1_el0", "pmceid1_el0"},
        AARCH64_LINUX_PMU_READ_BANK(0),
        AARCH64_LINUX_PMU_READ_BANK(1),
        AARCH64_LINUX_PMU_READ_BANK(2),
        AARCH64_LINUX_PMU_READ_BANK(3),
        AARCH64_LINUX_PMU_READ_BANK(4),
        AARCH64_LINUX_PMU_READ_BANK(5),
        AARCH64_LINUX_PMU_READ_BANK(6),
        AARCH64_LINUX_PMU_READ_BANK(7),
        AARCH64_LINUX_PMU_READ_BANK(8),
        AARCH64_LINUX_PMU_READ_BANK(9),
        AARCH64_LINUX_PMU_READ_BANK(10),
        AARCH64_LINUX_PMU_READ_BANK(11),
        AARCH64_LINUX_PMU_READ_BANK(12),
        AARCH64_LINUX_PMU_READ_BANK(13),
        AARCH64_LINUX_PMU_READ_BANK(14),
        AARCH64_LINUX_PMU_READ_BANK(15),
        AARCH64_LINUX_PMU_READ_BANK(16),
        AARCH64_LINUX_PMU_READ_BANK(17),
        AARCH64_LINUX_PMU_READ_BANK(18),
        AARCH64_LINUX_PMU_READ_BANK(19),
        AARCH64_LINUX_PMU_READ_BANK(20),
        AARCH64_LINUX_PMU_READ_BANK(21),
        AARCH64_LINUX_PMU_READ_BANK(22),
        AARCH64_LINUX_PMU_READ_BANK(23),
        AARCH64_LINUX_PMU_READ_BANK(24),
        AARCH64_LINUX_PMU_READ_BANK(25),
        AARCH64_LINUX_PMU_READ_BANK(26),
        AARCH64_LINUX_PMU_READ_BANK(27),
        AARCH64_LINUX_PMU_READ_BANK(28),
        AARCH64_LINUX_PMU_READ_BANK(29),
        AARCH64_LINUX_PMU_READ_BANK(30),
        /* Linux hw_breakpoint.h GEN_READ_WB_REG_CASES covers exactly 16
         * slots in each of the four architectural watch/breakpoint banks. */
        AARCH64_LINUX_DEBUG_READ_BANK(0),
        AARCH64_LINUX_DEBUG_READ_BANK(1),
        AARCH64_LINUX_DEBUG_READ_BANK(2),
        AARCH64_LINUX_DEBUG_READ_BANK(3),
        AARCH64_LINUX_DEBUG_READ_BANK(4),
        AARCH64_LINUX_DEBUG_READ_BANK(5),
        AARCH64_LINUX_DEBUG_READ_BANK(6),
        AARCH64_LINUX_DEBUG_READ_BANK(7),
        AARCH64_LINUX_DEBUG_READ_BANK(8),
        AARCH64_LINUX_DEBUG_READ_BANK(9),
        AARCH64_LINUX_DEBUG_READ_BANK(10),
        AARCH64_LINUX_DEBUG_READ_BANK(11),
        AARCH64_LINUX_DEBUG_READ_BANK(12),
        AARCH64_LINUX_DEBUG_READ_BANK(13),
        AARCH64_LINUX_DEBUG_READ_BANK(14),
        AARCH64_LINUX_DEBUG_READ_BANK(15),
        {"mrs%0,CurrentEL", "CurrentEL"},
        {"mrs%0,tcr_el1", "tcr_el1"},
        {"mrs%0,esr_el1", "esr_el1"},
        {"mrs%0,sctlr_el1", "sctlr_el1"},
        {"mrs%0,far_el1", "far_el1"},
        {"mrs%0,par_el1", "par_el1"},
        {"mrs%0,id_aa64mmfr1_el1", "id_aa64mmfr1_el1"},
        /* hyp/debug-sr.h derives the architectural breakpoint/watchpoint
         * bank sizes from ID_AA64DFR0_EL1, then saves MDCCINT_EL1. The
         * hyp sysreg save/restore path also reads these fixed registers. */
        {"mrs%0,id_aa64dfr0_el1", "id_aa64dfr0_el1"},
        /* fpsimd.c's SVE enable/SME setup intentionally uses Linux's
         * uppercase register-name macros, not the lowercase aliases. */
        {"mrs%0,CPACR_EL1", "CPACR_EL1"},
        {"mrs%0,SCTLR_EL1", "SCTLR_EL1"},
        {"mrs%0,mdccint_el1", "mdccint_el1"},
        {"mrs%0,tpidrro_el0", "tpidrro_el0"},
        {"mrs%0,sp_el1", "sp_el1"},
        {"mrs%0,vmpidr_el2", "vmpidr_el2"},
        {"mrs%0,hcr_el2", "hcr_el2"},
        /* KVM hyp/switch.h reads/restores these exact EL2 trap/debug
         * registers using the ordinary read_sysreg() GNU "=r" API. */
        {"mrs%0,fpexc32_el2", "fpexc32_el2"},
        {"mrs%0,cptr_el2", "cptr_el2"},
        {"mrs%0,elr_el2", "elr_el2"},
        {"mrs%0,dacr32_el2", "dacr32_el2"},
        {"mrs%0,dbgvcr32_el2", "dbgvcr32_el2"},
        {"mrs%0,ifsr32_el2", "ifsr32_el2"},
        {"mrs%0,pmuserenr_el0", "pmuserenr_el0"},
        {"mrs%0,spsr_fiq", "spsr_fiq"},
        {"mrs%0,spsr_irq", "spsr_irq"},
        {"mrs%0,spsr_und", "spsr_und"},
        {"mrs%0,hstr_el2", "hstr_el2"},
        /* KVM nVHE mem_protect.c obtains the true hardware IPA fault
         * address, not the FAR_EL1 alias or a fabricated zero value. */
        {"mrs%0,hpfar_el2", "hpfar_el2"},
        {"mrs%0,mdcr_el2", "mdcr_el2"},
        {"mrs%0,tpidr_el0", "tpidr_el0"},
        {"mrs%0,mdscr_el1", "mdscr_el1"},
        {"mrs%0,ttbr1_el1", "ttbr1_el1"},
        {"mrs%0,cnthctl_el2", "cnthctl_el2"},
        {"mrs%0,cpacr_el1", "cpacr_el1"},
        {"mrs%0,actlr_el1", "actlr_el1"},
        {"mrs%0,aidr_el1", "aidr_el1"},
        {"mrs%0,amair_el1", "amair_el1"},
        {"mrs%0,dbgauthstatus_el1", "dbgauthstatus_el1"},
        {"mrs%0,elr_el1", "elr_el1"},
        {"mrs%0,mair_el1", "mair_el1"},
        {"mrs%0,midr_el1", "midr_el1"},
        {"mrs%0,revidr_el1", "revidr_el1"},
        {"mrs%0,s3_0_c15_c0_5", "s3_0_c15_c0_5"},
        {"mrs%0,spsr_abt", "spsr_abt"},
        {"mrs%0,ttbr0_el1", "ttbr0_el1"},
        /* Linux tegra194-cpufreq.c: implementation-defined CPU NDIV. */
        {"mrs%0,s3_0_c15_c0_4", "s3_0_c15_c0_4"},
        {"mrs%0,daif", "daif"}
    };
    if (text == NULL || len >= sizeof(normalized))
        return NULL;
    for (i = 0U; i < len; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            continue;
        if (used + 1U >= sizeof(normalized))
            return NULL;
        normalized[used++] = c;
    }
    normalized[used] = '\0';
    for (i = 0U; i < sizeof(allowed) / sizeof(allowed[0]); ++i)
        if (strcmp(allowed[i].text, normalized) == 0)
            return allowed[i].reg;
    return NULL;
}
#undef AARCH64_LINUX_PMU_READ_BANK
#undef AARCH64_LINUX_DEBUG_READ_BANK
/* The architectural DMB/DSB/ISB operations are hardware fences; they
 * cannot be silently downgraded into a compiler-only memory clobber.
 * The source and machine both use this target-owned exact template table. */
const char *aarch64_barrier_template(const char *text, size_t len) {
    static const char *const allowed[] = {
        "dmb ish", "dmb ishld", "dmb ishst", "dmb sy",
        "dmb osh", "dmb oshld", "dmb oshst",
        "dsb sy", "dsb ish", "dsb ishst", "dsb ld", "dsb st",
        "dsb nsh", "dsb nshst", "isb",
        "msr daifset, #3", "msr daifclr, #3", "msr daifclr, #8",
        /* barrier.h psb_csync()/tsb_csync(): architected trace drain hints. */
        "yield", "hint #6", "hint #17", "hint #18", "hint #20",
        "sev", "sevl", "wfe", "wfi", "nop",
        "ic ialluis", "ic iallu"
    };
    char normalized[64];
    size_t i, n = 0U;
    if (text == NULL || len == 0U || len >= sizeof(normalized))
        return NULL;
    /* Original Linux cache.h writes the real IC operand with a tab. The
     * opcode is unique and privilege-sensitive; keep only this exact tabbed
     * spelling, without admitting tabs/newlines in other hardware ASM. */
    if (len == sizeof("ic\tialluis") - 1U &&
        memcmp(text, "ic\tialluis", len) == 0)
        return "ic ialluis";
    if (len == sizeof("ic\tiallu") - 1U &&
        memcmp(text, "ic\tiallu", len) == 0)
        return "ic iallu";
    for (i = 0U; i < len; ++i) {
        const char c = text[i];
        if (c == '\t' || c == '\r' || c == '\n')
            return NULL; /* no hidden second hardware instruction */
        if (n + 1U >= sizeof(normalized))
            return NULL;
        normalized[n++] = c;
    }
    while (n != 0U && normalized[n - 1U] == ' ') --n;
    normalized[n] = '\0';
    for (i = 0U; i < sizeof(allowed) / sizeof(allowed[0]); ++i)
        if (strcmp(normalized, allowed[i]) == 0)
            return allowed[i];
    return NULL;
}

/* Original Linux arm64 sysreg.h write_sysreg(): a privileged hardware
 * effect with one 64-bit rZ input. Keep the exact named EL0/EL1/EL2
 * register allowlist and reject arbitrary substituted GNU ASM. Source and
 * machine emission consult this one target ISA contract. */
const char *aarch64_linux_msr_register(const char *text, size_t len) {
    /* Linux debug-sr.h expands restore_debug() into the full 16-slot
     * architectural breakpoint/watchpoint register bank.  Keep the
     * finite set of named GNU registers, never a generic `msr %s` gate. */
#define AARCH64_LINUX_DEBUG_WRITES(n) \
    "dbgbcr" #n "_el1", "dbgbvr" #n "_el1", \
    "dbgwcr" #n "_el1", "dbgwvr" #n "_el1"
    /* PMUv3 event registers are architecturally indexed 0..30; KVM and
     * arm_pmuv3.c write both counter and event-selector banks. */
#define AARCH64_LINUX_PMU_WRITES(n) \
    "pmevcntr" #n "_el0", "pmevtyper" #n "_el0"
    static const char *const registers[] = {
        "daif", "ttbr0_el1", "ttbr1_el1", "tcr_el1", "sctlr_el1",
        "contextidr_el1", "cpacr_el1", "vbar_el1", "mdscr_el1",
        "osdlr_el1", "oslar_el1", "cntkctl_el1",
        "cntp_ctl_el0", "cntp_cval_el0", "cntv_ctl_el0",
        "cntv_cval_el0", "tpidr_el0", "tpidrro_el0",
        "tpidr_el2", "hcr_el2", "cptr_el2", "vtcr_el2",
        "vttbr_el2", "fpexc32_el2", "hstr_el2", "mdcr_el2",
        /* Linux 6.6.143 KVM hyp/PMU writes use the same one-input rZ
         * sysreg contract.  These names are GNU AArch64 hardware operands,
         * not ordinary C variables or aliases; preserve the MSR effect. */
        "par_el1", "elr_el2", "spsr_el2", "spsr_abt", "spsr_und",
        "vmpidr_el2", "cnthctl_el2", "cntvoff_el2", "pmuserenr_el0",
        "pmcntenset_el0", "pmcntenclr_el0", "pmovsclr_el0",
        "pmselr_el0",
        "pmintenset_el1", "pmintenclr_el1", "pmccntr_el0",
        "pmccfiltr_el0", "pmcr_el0", "CPACR_EL1",
        "mdccint_el1", "tpidr_el1", "sp_el1",
        "SCTLR_EL1",
        "spsr_irq", "spsr_fiq", "dacr32_el2", "ifsr32_el2",
        "dbgvcr32_el2", "mair_el1", "vbar_el2",
        AARCH64_LINUX_DEBUG_WRITES(0), AARCH64_LINUX_DEBUG_WRITES(1),
        AARCH64_LINUX_DEBUG_WRITES(2), AARCH64_LINUX_DEBUG_WRITES(3),
        AARCH64_LINUX_DEBUG_WRITES(4), AARCH64_LINUX_DEBUG_WRITES(5),
        AARCH64_LINUX_DEBUG_WRITES(6), AARCH64_LINUX_DEBUG_WRITES(7),
        AARCH64_LINUX_DEBUG_WRITES(8), AARCH64_LINUX_DEBUG_WRITES(9),
        AARCH64_LINUX_DEBUG_WRITES(10), AARCH64_LINUX_DEBUG_WRITES(11),
        AARCH64_LINUX_DEBUG_WRITES(12), AARCH64_LINUX_DEBUG_WRITES(13),
        AARCH64_LINUX_DEBUG_WRITES(14), AARCH64_LINUX_DEBUG_WRITES(15),
        AARCH64_LINUX_PMU_WRITES(0), AARCH64_LINUX_PMU_WRITES(1),
        AARCH64_LINUX_PMU_WRITES(2), AARCH64_LINUX_PMU_WRITES(3),
        AARCH64_LINUX_PMU_WRITES(4), AARCH64_LINUX_PMU_WRITES(5),
        AARCH64_LINUX_PMU_WRITES(6), AARCH64_LINUX_PMU_WRITES(7),
        AARCH64_LINUX_PMU_WRITES(8), AARCH64_LINUX_PMU_WRITES(9),
        AARCH64_LINUX_PMU_WRITES(10), AARCH64_LINUX_PMU_WRITES(11),
        AARCH64_LINUX_PMU_WRITES(12), AARCH64_LINUX_PMU_WRITES(13),
        AARCH64_LINUX_PMU_WRITES(14), AARCH64_LINUX_PMU_WRITES(15),
        AARCH64_LINUX_PMU_WRITES(16), AARCH64_LINUX_PMU_WRITES(17),
        AARCH64_LINUX_PMU_WRITES(18), AARCH64_LINUX_PMU_WRITES(19),
        AARCH64_LINUX_PMU_WRITES(20), AARCH64_LINUX_PMU_WRITES(21),
        AARCH64_LINUX_PMU_WRITES(22), AARCH64_LINUX_PMU_WRITES(23),
        AARCH64_LINUX_PMU_WRITES(24), AARCH64_LINUX_PMU_WRITES(25),
        AARCH64_LINUX_PMU_WRITES(26), AARCH64_LINUX_PMU_WRITES(27),
        AARCH64_LINUX_PMU_WRITES(28), AARCH64_LINUX_PMU_WRITES(29),
        AARCH64_LINUX_PMU_WRITES(30)
    };
#undef AARCH64_LINUX_DEBUG_WRITES
#undef AARCH64_LINUX_PMU_WRITES
    char compact[96];
    size_t i, n = 0U;
    if (!text || !len || len >= sizeof(compact)) return NULL;
    for (i = 0U; i < len; ++i) {
        const char c = text[i];
        if (c == '\r' || c == '\n' || c == ';' || c == '\0') return NULL;
        if (c == ' ' || c == '\t') continue;
        if (n + 1U >= sizeof(compact)) return NULL;
        compact[n++] = c;
    }
    compact[n] = '\0';
    /* Tegra194 uses GNU "r" with %0 instead of write_sysreg's rZ/%x0.
     * This exact SoC register is the sole permitted vendor exception. */
    if (strcmp(compact, "msrs3_0_c15_c0_4,%0") == 0)
        return "s3_0_c15_c0_4";
    for (i = 0U; i < sizeof(registers) / sizeof(registers[0]); ++i) {
        const size_t rlen = strlen(registers[i]);
        if (n == rlen + sizeof("msr,%x0") - 1U &&
            memcmp(compact, "msr", 3U) == 0 &&
            memcmp(compact + 3U, registers[i], rlen) == 0 &&
            memcmp(compact + 3U + rlen, ",%x0", 4U) == 0)
            return registers[i];
    }
    return NULL;
}



/* Exact original Linux 6.6.143 arm64 sysreg.h read_sysreg_s SYS_*:
 * a full GNU .irp/.equ/.macro/.inst/.purgem expansion, NOT a simple MRS.
 * Keep all source directives and SYS_* hardware encodings on target side.
 * Never pass privileged GNU macro statements through unqualified. */
unsigned int aarch64_linux_encoded_sysreg_form(
    const char *text, size_t length) {
    static const char prefix[] = ".irpnum,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30.equ.L__gpr_num_x\\num,\\num.equ.L__gpr_num_w\\num,\\num.endr.equ.L__gpr_num_xzr,31.equ.L__gpr_num_wzr,31.macromrs_s,rt,sreg.inst(0xd5200000|(\\sreg)|(.L__gpr_num_\\rt)).endm";
    static const char suffix[] = ".purgemmrs_s";
    char normalized[768],expected[768];
    unsigned int op0,op1,crn,crm,op2;
    int parsed=0, count;
    size_t i,n=0U,nesting,prefix_len=sizeof(prefix)-1U;
    if(!text||!length||length>=sizeof(normalized))return 0U;
    for(i=0U;i<length;++i){
        const char ch=text[i];
        if(ch==' '||ch=='\t'||ch=='\r'||ch=='\n')continue;
        if(ch=='\0'||n+1U>=sizeof(normalized))return 0U;
        normalized[n++]=ch;
    }
    normalized[n]='\0';
    if(n<=prefix_len || memcmp(normalized,prefix,prefix_len)!=0)return 0U;
    /* Linux 6.6 topology.c AMU core/constant frequency counters use the
     * actual SYS_AMEVCNTR0{0,1}_EL0 macro, not a flattened numeric tuple.
     * Preserve the precise CRm=(4+(n>>3)), op2=(n&7) expression and the
     * entire GNU GPR/.irp/MRS_S preamble; no general constant-expression
     * assembler passthrough is permitted here. */
    for(i=0U;i<=1U;++i){
        count=snprintf(expected,sizeof(expected),
            "%smrs_s%%0,(((3)<<19)|((3)<<16)|((13)<<12)|"
            "(((4+((%zu)>>3)))<<8)|((((%zu)&7))<<5))%s",
            prefix,i,i,suffix);
        if(count>0&&(size_t)count<sizeof(expected)&&
           strcmp(normalized,expected)==0)
            return 0xd5200000U | (3U<<19) | (3U<<16) |
                   (13U<<12) | (4U<<8) | ((unsigned int)i<<5) | 9U;
    }
    /* Linux SYS_ZCR_EL2 passes SYS_* through an extra parenthesized macro
     * argument. Accept exactly zero or one complete outer grouping around
     * the five-field tuple; keep the original GNU directive prefix intact. */
    for(nesting=0U;nesting<=1U;++nesting){
        parsed=0;
        if(sscanf(normalized+prefix_len,
              nesting ?
              "mrs_s%%0,((((%u)<<19)|((%u)<<16)|((%u)<<12)|"
              "((%u)<<8)|((%u)<<5))).purgemmrs_s%n" :
              "mrs_s%%0,(((%u)<<19)|((%u)<<16)|((%u)<<12)|"
              "((%u)<<8)|((%u)<<5)).purgemmrs_s%n",
              &op0,&op1,&crn,&crm,&op2,&parsed)!=5 || parsed<=0 ||
           normalized[prefix_len+(size_t)parsed]!='\0' ||
           (op0!=3U && !(op0==2U && op1==4U && crn==0U &&
                          crm==7U && op2==0U)) ||
           op1>7U || crn>15U || crm>15U || op2>7U)
            continue;
        count=snprintf(expected,sizeof(expected),
            "%smrs_s%%0,%s(((%u)<<19)|((%u)<<16)|((%u)<<12)|"
            "((%u)<<8)|((%u)<<5))%s%s",
            prefix,nesting?"(":"",op0,op1,crn,crm,op2,
            nesting?")":"",suffix);
        if(count>0&&(size_t)count<sizeof(expected)&&
           strcmp(normalized,expected)==0)
            return 0xd5200000U | (op0<<19) | (op1<<16) |
                   (crn<<12) | (crm<<8) | (op2<<5) | 9U;
    }
    return 0U;
}


/* Hardware-visible privileged SYS register effects remain an explicit
 * target ISA catalog. GICv3 ICC_* accesses have the same GNU .irp macro
 * template as ICC_SRE_EL1, with distinct encoded register fields. */
const Aarch64LinuxWriteSysreg aarch64_linux_write_sysregs[] = {
    {3U,0U,12U,12U,5U}, /* ICC_SRE_EL1 */
    /* Apple AIC fast-IPI, virtual-timer FIQ and per-core/uncore PMC
     * hardware controls. Linux 6.6 irq-apple-aic.c encodes each through
     * the full sysreg.h MSR_S GNU .irp/.equ/.inst macro, never free-form
     * inline assembler. Keep the six implementation-defined S3 tuples
     * finite; all unrelated S3_C15 registers continue to fail closed. */
    {3U,5U,15U,0U,0U}, /* IMP_APL_IPI_RR_LOCAL_EL1 */
    {3U,5U,15U,0U,1U}, /* IMP_APL_IPI_RR_GLOBAL_EL1 */
    {3U,5U,15U,1U,1U}, /* IMP_APL_IPI_SR_EL1 */
    {3U,5U,15U,1U,3U}, /* IMP_APL_VM_TMR_FIQ_ENA_EL2 */
    {3U,1U,15U,0U,0U}, /* IMP_APL_PMCR0_EL1 */
    {3U,7U,15U,0U,4U}, /* IMP_APL_UPMCR0_EL1 */
    {3U,0U,4U,6U,0U},  /* ICC_PMR_EL1 */
    {3U,3U,4U,2U,2U},  /* SVCR */
    {3U,0U,1U,2U,4U},  /* SMPRI_EL1: SME priority control */
    {3U,0U,1U,2U,6U},  /* SMCR_EL1: SME vector controls */
    {3U,4U,1U,2U,6U},  /* SMCR_EL2 */
    {3U,5U,1U,2U,6U},  /* SMCR_EL12 */
    {3U,3U,13U,0U,5U}, /* TPIDR2_EL0 */
    /* kvm_host.h __vcpu_write_sys_reg_to_cpu(): complete VHE-only
     * runtime switch includes these seven additional SYS_* writes. */
    {3U,3U,13U,0U,2U}, /* TPIDR_EL0 */
    {3U,3U,13U,0U,3U}, /* TPIDRRO_EL0 */
    {3U,0U,13U,0U,4U}, /* TPIDR_EL1 */
    {3U,0U,7U,4U,0U},  /* PAR_EL1 */
    {3U,4U,3U,0U,0U},  /* DACR32_EL2 */
    {3U,4U,5U,0U,1U},  /* IFSR32_EL2 */
    {2U,4U,0U,7U,0U},  /* DBGVCR32_EL2 */
    /* Qualcomm Kryo's original Linux indirect L2 accessors use two
     * vendor-defined S3_3_C15_C0 registers, both authenticated against
     * the full GNU MSR_S wrapper and these exact tuples. */
    {3U,3U,15U,0U,6U}, /* L2CPUSRSELR_EL1 */
    {3U,3U,15U,0U,7U}, /* L2CPUSRDR_EL1 */
    {3U,0U,12U,11U,1U}, /* ICC_DIR_EL1 */
    {3U,0U,12U,11U,5U}, /* ICC_SGI1R_EL1 */
    {3U,0U,12U,12U,1U}, /* ICC_EOIR1_EL1 */
    {3U,0U,12U,12U,3U}, /* ICC_BPR1_EL1 */
    {3U,0U,12U,12U,4U}, /* ICC_CTLR_EL1 */
    {3U,0U,12U,12U,7U}, /* ICC_IGRPEN1_EL1 */
    /* Linux sysreg.h SYS_ICC_AP0Rn is CRm=8, op2=(4|n);
     * SYS_ICC_AP1Rn is CRm=9, op2=n (previous table was reversed). */
    {3U,0U,12U,8U,4U}, /* ICC_AP0R0_EL1 */
    {3U,0U,12U,8U,5U}, /* ICC_AP0R1_EL1 */
    {3U,0U,12U,8U,6U}, /* ICC_AP0R2_EL1 */
    {3U,0U,12U,8U,7U}, /* ICC_AP0R3_EL1 */
    {3U,0U,12U,9U,0U}, /* ICC_AP1R0_EL1 */
    {3U,0U,12U,9U,1U}, /* ICC_AP1R1_EL1 */
    {3U,0U,12U,9U,2U}, /* ICC_AP1R2_EL1 */
    {3U,0U,12U,9U,3U}, /* ICC_AP1R3_EL1 */
    {3U,4U,12U,9U,5U}, /* ICC_SRE_EL2 */
    /* Linux 6.6 arch/arm64/tools/sysreg + KVM hyp/switch.h: explicit
     * hypervisor trap controls. Each exact 5-field instruction encoding
     * is lowered as privileged MSR, never as a discarded GNU macro. */
    {3U,4U,1U,1U,4U}, /* HFGRTR_EL2 */
    {3U,4U,1U,1U,5U}, /* HFGWTR_EL2 */
    {3U,4U,1U,1U,6U}, /* HFGITR_EL2 */
    {3U,4U,3U,1U,4U}, /* HDFGRTR_EL2 */
    {3U,4U,3U,1U,5U}, /* HDFGWTR_EL2 */
    {3U,4U,1U,2U,2U}, /* HCRX_EL2 */
    {3U,4U,5U,2U,3U}, /* VSESR_EL2 */
    /* Linux original KVM PAUTH key save/restore: ten architectural
     * SYS_AP*KEY*EL1 names, with exact op1/CRn/CRm/op2 pairs. */
    {3U,0U,2U,1U,0U}, /* APIAKEYLO_EL1 */
    {3U,0U,2U,1U,1U}, /* APIAKEYHI_EL1 */
    {3U,0U,2U,1U,2U}, /* APIBKEYLO_EL1 */
    {3U,0U,2U,1U,3U}, /* APIBKEYHI_EL1 */
    {3U,0U,2U,2U,0U}, /* APDAKEYLO_EL1 */
    {3U,0U,2U,2U,1U}, /* APDAKEYHI_EL1 */
    {3U,0U,2U,2U,2U}, /* APDBKEYLO_EL1 */
    {3U,0U,2U,2U,3U}, /* APDBKEYHI_EL1 */
    {3U,0U,2U,3U,0U}, /* APGAKEYLO_EL1 */
    {3U,0U,2U,3U,1U}, /* APGAKEYHI_EL1 */
    /* The real Linux 6.6 KVM protected-hyp and VHE sources use these
     * additional architectural system registers through the same GNU
     * write_sysreg_s() machinery.  The tuple is the hardware encoding;
     * these are not template-string or function-name based exceptions.
     * EL1 control, translation, exception, timer and debug state. */
    {3U,0U,1U,0U,0U}, /* SCTLR_EL1 */
    {3U,0U,1U,0U,2U}, /* CPACR_EL1 */
    {3U,0U,1U,0U,5U}, /* RGSR_EL1 */
    {3U,0U,1U,0U,6U}, /* GCR_EL1 */
    {3U,0U,1U,2U,0U}, /* ZCR_EL1 */
    {3U,0U,1U,2U,1U}, /* TRFCR_EL1 */
    {3U,0U,12U,1U,1U}, /* DISR_EL1 */
    {3U,0U,2U,0U,0U}, /* TTBR0_EL1 */
    {3U,0U,2U,0U,1U}, /* TTBR1_EL1 */
    {3U,0U,2U,0U,2U}, /* TCR_EL1 */
    {3U,0U,2U,0U,3U}, /* TCR2_EL1 */
    {3U,0U,4U,0U,0U}, /* SPSR_EL1 */
    {3U,0U,4U,0U,1U}, /* ELR_EL1 */
    {3U,0U,5U,2U,0U}, /* ESR_EL1 */
    {3U,4U,5U,2U,0U}, /* ESR_EL2: KVM VHE ESR_EL2/ESR_EL1 patch pair */
    {3U,0U,5U,1U,0U}, /* AFSR0_EL1 */
    {3U,0U,5U,1U,1U}, /* AFSR1_EL1 */
    {3U,0U,5U,6U,0U}, /* TFSR_EL1 */
    {3U,0U,5U,6U,1U}, /* TFSRE0_EL1 */
    {3U,0U,6U,0U,0U}, /* FAR_EL1 */
    {3U,4U,6U,0U,0U}, /* FAR_EL2: KVM VHE FAR_EL2/FAR_EL1 patch pair */
    {3U,0U,10U,2U,0U}, /* MAIR_EL1 */
    {3U,0U,10U,2U,2U}, /* PIRE0_EL1 */
    {3U,0U,10U,2U,3U}, /* PIR_EL1 */
    {3U,0U,10U,3U,0U}, /* AMAIR_EL1 */
    {3U,0U,12U,0U,0U}, /* VBAR_EL1 */
    {3U,0U,13U,0U,1U}, /* CONTEXTIDR_EL1 */
    {3U,0U,14U,1U,0U}, /* CNTKCTL_EL1 */
    {3U,0U,9U,9U,0U}, /* PMSCR_EL1 */
    /* EL2 control, stage-2 translation, virtual GIC and timer registers. */
    {3U,4U,1U,0U,0U}, /* SCTLR_EL2 */
    {3U,4U,1U,2U,0U}, /* ZCR_EL2 */
    {3U,4U,12U,1U,1U}, /* DISR_EL2 */
    {3U,4U,12U,11U,0U}, /* ICH_HCR_EL2 */
    {3U,4U,12U,11U,7U}, /* ICH_VMCR_EL2 */
    /* arch/arm64/include/asm/sysreg.h: AP0R is CRm=8, AP1R is CRm=9;
     * list registers LR0..7 are CRm=12, LR8..15 are CRm=13. The former
     * abbreviated table accidentally swapped CRm for these families,
     * producing wrong hardware effects despite syntactically valid .inst. */
    {3U,4U,12U,8U,0U}, /* ICH_AP0R0_EL2 */
    {3U,4U,12U,8U,1U}, /* ICH_AP0R1_EL2 */
    {3U,4U,12U,8U,2U}, /* ICH_AP0R2_EL2 */
    {3U,4U,12U,8U,3U}, /* ICH_AP0R3_EL2 */
    {3U,4U,12U,9U,0U}, /* ICH_AP1R0_EL2 */
    {3U,4U,12U,9U,1U}, /* ICH_AP1R1_EL2 */
    {3U,4U,12U,9U,2U}, /* ICH_AP1R2_EL2 */
    {3U,4U,12U,9U,3U}, /* ICH_AP1R3_EL2 */
    {3U,4U,12U,12U,0U}, /* ICH_LR0_EL2 */
    {3U,4U,12U,12U,1U}, /* ICH_LR1_EL2 */
    {3U,4U,12U,12U,2U}, /* ICH_LR2_EL2 */
    {3U,4U,12U,12U,3U}, /* ICH_LR3_EL2 */
    {3U,4U,12U,12U,4U}, /* ICH_LR4_EL2 */
    {3U,4U,12U,12U,5U}, /* ICH_LR5_EL2 */
    {3U,4U,12U,12U,6U}, /* ICH_LR6_EL2 */
    {3U,4U,12U,12U,7U}, /* ICH_LR7_EL2 */
    {3U,4U,12U,13U,0U}, /* ICH_LR8_EL2 */
    {3U,4U,12U,13U,1U}, /* ICH_LR9_EL2 */
    {3U,4U,12U,13U,2U}, /* ICH_LR10_EL2 */
    {3U,4U,12U,13U,3U}, /* ICH_LR11_EL2 */
    {3U,4U,12U,13U,4U}, /* ICH_LR12_EL2 */
    {3U,4U,12U,13U,5U}, /* ICH_LR13_EL2 */
    {3U,4U,12U,13U,6U}, /* ICH_LR14_EL2 */
    {3U,4U,12U,13U,7U}, /* ICH_LR15_EL2 */
    {3U,4U,14U,0U,6U}, /* CNTVOFF_EL2 */
    /* KVM arch_timer.c feature-47 VHE traps switch EL0 timer access
     * between true EL0 S3_3 and EL02 S3_5 aliases. These four pairs
     * must retain their exact read/write direction and patch metadata. */
    {3U,3U,14U,2U,1U}, /* CNTP_CTL_EL0 */
    {3U,5U,14U,2U,1U}, /* CNTP_CTL_EL02 */
    {3U,3U,14U,2U,2U}, /* CNTP_CVAL_EL0 */
    {3U,3U,14U,3U,1U}, /* CNTV_CTL_EL0 */
    {3U,5U,14U,3U,1U}, /* CNTV_CTL_EL02 */
    {3U,3U,14U,3U,2U}, /* CNTV_CVAL_EL0 */
    {3U,5U,14U,3U,2U}, /* CNTV_CVAL_EL02 */
    {3U,4U,4U,0U,0U}, /* SPSR_EL2 */
    {3U,4U,4U,0U,1U}, /* ELR_EL2 */
    {3U,5U,1U,0U,0U}, /* SCTLR_EL12 */
    {3U,5U,1U,0U,2U}, /* CPACR_EL12 */
    {3U,5U,1U,2U,0U}, /* ZCR_EL12 */
    {3U,5U,14U,2U,2U}, /* EL12 virtualized timer control */
    {3U,5U,2U,0U,0U}, /* TTBR0_EL12 */
    {3U,5U,2U,0U,1U}, /* TTBR1_EL12 */
    {3U,5U,2U,0U,2U}, /* TCR_EL12 */
    {3U,5U,2U,0U,3U}, /* TCR2_EL12 */
    {3U,5U,4U,0U,0U}, /* SPSR_EL12 */
    {3U,5U,4U,0U,1U}, /* ELR_EL12 */
    {3U,5U,5U,1U,0U}, /* AFSR0_EL12 */
    {3U,5U,5U,1U,1U}, /* AFSR1_EL12 */
    {3U,5U,5U,2U,0U}, /* ESR_EL12 */
    {3U,5U,5U,6U,0U}, /* TFSR_EL12 */
    {3U,5U,6U,0U,0U}, /* FAR_EL12 */
    {3U,5U,10U,2U,0U}, /* MAIR_EL12 */
    {3U,5U,10U,2U,2U}, /* PIRE0_EL12 */
    {3U,5U,10U,2U,3U}, /* PIR_EL12 */
    {3U,5U,10U,3U,0U}, /* AMAIR_EL12 */
    {3U,5U,12U,0U,0U}, /* VBAR_EL12 */
    {3U,5U,13U,0U,1U}, /* CONTEXTIDR_EL12 */
    {3U,5U,14U,1U,0U}  /* CNTKCTL_EL12 */
};

/* Linux 6.6 arm64 sysreg.h write_sysreg_s() is the WRITE companion to
 * read_sysreg_s() above. Its real source uses .irp, .equ and a GNU macro
 * to assemble a privileged MSR system-register opcode. Unlike a direct
 * `msr foo, xN`, the named sreg is a constant expression encoded inside
 * .inst; preserving the complete verified expression is mandatory.
 * The kernel GICv3 SRE register is S3_0_C12_C12_5. Other arbitrary
 * opcodes and unknown SYS_* values remain outside this exact catalog. */
unsigned int aarch64_linux_encoded_sysreg_write_form(
    const char *text, size_t length) {
    static const char prefix[] =
        ".irpnum,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30"
        ".equ.L__gpr_num_x\\num,\\num"
        ".equ.L__gpr_num_w\\num,\\num"
        ".endr"
        ".equ.L__gpr_num_xzr,31"
        ".equ.L__gpr_num_wzr,31"
        ".macromsr_s,sreg,rt"
        ".inst(0xd5000000|(\\sreg)|(.L__gpr_num_\\rt))"
        ".endm";
    char compact[768], expected[768];
    size_t i, used = 0U, nesting;
    if (!text || !length || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || used + 1U >= sizeof(compact)) return 0U;
        compact[used++] = c;
    }
    compact[used] = '\0';
    for (i = 0U; i < sizeof(aarch64_linux_write_sysregs) /
                       sizeof(aarch64_linux_write_sysregs[0]); ++i) {
        const Aarch64LinuxWriteSysreg *reg = &aarch64_linux_write_sysregs[i];
        char op2_source[24];
        const int op2_n = reg->crn == 12U && reg->crm == 8U &&
                          reg->op2 >= 4U && reg->op2 <= 7U
            ? snprintf(op2_source, sizeof(op2_source), "4|%u", reg->op2 - 4U)
            : snprintf(op2_source, sizeof(op2_source), "%u", reg->op2);
        if (op2_n < 1 || (size_t)op2_n >= sizeof(op2_source)) return 0U;
        for (nesting = 0U; nesting <= 1U; ++nesting) {
            const int n = snprintf(expected, sizeof(expected),
                "%smsr_s%s(((%u)<<19)|((%u)<<16)|((%u)<<12)|"
                "((%u)<<8)|((%s)<<5))%s,%%x0.purgemmsr_s",
                prefix, nesting ? "(" : "", reg->op0, reg->op1,
                reg->crn, reg->crm, op2_source, nesting ? ")" : "");
            if (n > 0 && (size_t)n < sizeof(expected) &&
                strcmp(compact, expected) == 0)
                return (unsigned int)i + 1U;
        }
    }
    return 0U;
}




const size_t aarch64_linux_write_sysreg_count =
    sizeof(aarch64_linux_write_sysregs) / sizeof(aarch64_linux_write_sysregs[0]);
