/* Linux 6.6.143 arch/loongarch/kernel/traps.c:do_vint IRQ-stack switch.
 * The compiler must save its own RA and S0 across this inline BL. */
struct pt_regs { unsigned long regs[32]; };
extern void handle_loongarch_irq(struct pt_regs *regs);

void loong_irq_switch(unsigned long stack, struct pt_regs *regs) {
    __asm__ __volatile__(
        "move\t$s0, $sp\t\t\n"
        "move\t$sp, %[stk]\t\t\n"
        "move\t$a0, %[regs]\t\t\n"
        "bl\thandle_loongarch_irq\n"
        "move\t$sp, $s0\t\t\n"
        :
        : [stk] "r" (stack), [regs] "r" (regs)
        : "$a0", "$a1", "$a2", "$a3", "$a4", "$a5", "$a6", "$a7", "$s0",
          "$t0", "$t1", "$t2", "$t3", "$t4", "$t5", "$t6", "$t7", "$t8",
          "memory");
}
