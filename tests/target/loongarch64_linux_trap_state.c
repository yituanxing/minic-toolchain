/* LoongArch Linux 6.6 IRQ stack switch and live trap-frame register dump.
 * Compile-only: privileged CSRs and a foreign IRQ stack are not runnable. */
struct linux_pt_regs {
    unsigned long regs[32];
    unsigned long csr_era;
    unsigned long csr_badvaddr;
    unsigned long csr_crmd;
    unsigned long csr_prmd;
    unsigned long csr_euen;
    unsigned long csr_ecfg;
    unsigned long csr_estat;
};

extern void handle_loongarch_irq(struct linux_pt_regs *);

void linux_irq_stack_switch(unsigned long stack, struct linux_pt_regs *regs) {
    __asm__ __volatile__(
        "move\t$s0, $sp\n"
        "move\t$sp, %[stk]\n"
        "move\t$a0, %[regs]\n"
        "bl\thandle_loongarch_irq\n"
        "move\t$sp, $s0\n"
        : : [stk] "r" (stack), [regs] "r" (regs)
        : "$a0", "$a1", "$a2", "$a3", "$a4", "$a5", "$a6", "$a7",
          "$s0", "$t0", "$t1", "$t2", "$t3", "$t4", "$t5", "$t6",
          "$t7", "$t8", "memory");
}

#define LINUX_SAVE_GPR(num) "st.d $r" #num ",%1,8*" #num "\n\t"

void linux_prepare_frametrace(struct linux_pt_regs *regs) {
    __asm__ __volatile__(
        LINUX_SAVE_GPR(1)
        "pcaddi $ra, 0\n\t"
        "st.d $ra, %0\n\t"
        "ld.d $ra, %1, 8\n\t"
        LINUX_SAVE_GPR(2)  LINUX_SAVE_GPR(3)  LINUX_SAVE_GPR(4)
        LINUX_SAVE_GPR(5)  LINUX_SAVE_GPR(6)  LINUX_SAVE_GPR(7)
        LINUX_SAVE_GPR(8)  LINUX_SAVE_GPR(9)  LINUX_SAVE_GPR(10)
        LINUX_SAVE_GPR(11) LINUX_SAVE_GPR(12) LINUX_SAVE_GPR(13)
        LINUX_SAVE_GPR(14) LINUX_SAVE_GPR(15) LINUX_SAVE_GPR(16)
        LINUX_SAVE_GPR(17) LINUX_SAVE_GPR(18) LINUX_SAVE_GPR(19)
        LINUX_SAVE_GPR(20) LINUX_SAVE_GPR(21) LINUX_SAVE_GPR(22)
        LINUX_SAVE_GPR(23) LINUX_SAVE_GPR(24) LINUX_SAVE_GPR(25)
        LINUX_SAVE_GPR(26) LINUX_SAVE_GPR(27) LINUX_SAVE_GPR(28)
        LINUX_SAVE_GPR(29) LINUX_SAVE_GPR(30) LINUX_SAVE_GPR(31)
        : "=m" (regs->csr_era) : "r" (regs->regs) : "memory");
    __asm__ __volatile__("csrrd %0, 0x7\n\t" : "=r" (regs->csr_badvaddr));
    __asm__ __volatile__("csrrd %0, 0x0\n\t" : "=r" (regs->csr_crmd));
    __asm__ __volatile__("csrrd %0, 0x1\n\t" : "=r" (regs->csr_prmd));
    __asm__ __volatile__("csrrd %0, 0x2\n\t" : "=r" (regs->csr_euen));
    __asm__ __volatile__("csrrd %0, 0x4\n\t" : "=r" (regs->csr_ecfg));
    __asm__ __volatile__("csrrd %0, 0x5\n\t" : "=r" (regs->csr_estat));
}

void linux_acknowledge_fp_exception(unsigned long fcsr) {
    __asm__ __volatile__("movgr2fcsr $fcsr0, %0\n\t" : : "r" (fcsr));
}
