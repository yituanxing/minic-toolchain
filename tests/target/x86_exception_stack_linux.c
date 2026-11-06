/* Exact Linux 6.6 irq_stack.h / page_fault_oops source-template families.
 * Every object remains an original GNU C inline-ASM expression; an x86
 * native assembler must materialize .discard.reachable and direct calls. */
extern void irq_enter_rcu(void);
extern void irq_exit_rcu(void);
extern void exception_target(void *, unsigned long, void *);
extern void irq_target1(void *);
extern void irq_target2(void *, unsigned long);
extern void softirq_target(void);
register unsigned long sp asm("rsp");

#define STACK_CLOBBERS "cc", "rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "memory"

void test_exception_stack(void *stack_top, void *first,
    unsigned long second, void *third) {
    register void *tos asm("r11") = stack_top;
    __asm__ __volatile__(
        "movq %%rsp, (%[tos])\n\t"
        "movq %[tos], %%rsp\n\t"
        "movq %[arg3], %%rdx\n\t"
        "movq %[arg2], %%rsi\n\t"
        "movq %[arg1], %%rdi\n\t"
        "call %c[__func]\n\t"
        "998:\n\t"
        ".pushsection .discard.reachable\n\t"
        ".long 998b\n\t"
        ".popsection\n\t"
        "popq %%rsp\n\t"
        : [tos] "+r"(tos), [sp] "+r"(sp)
        : [__func] "i"(exception_target), [top] "r"(tos),
          [arg1] "r"(first), [arg2] "r"(second), [arg3] "r"(third)
        : STACK_CLOBBERS);
}

#ifndef MINIC_TEST_EXCEPTION_ONLY
void test_irq1(void *stack_top, void *arg1) {
    register void *tos asm("r11") = stack_top;
    __asm__ __volatile__(
        "movq %%rsp, (%[tos])\n\t"
        "movq %[tos], %%rsp\n\t"
        "call irq_enter_rcu\n\t"
        "movq %[arg1], %%rdi\n\t"
        "call %c[__func]\n\t"
        "998:\n\t"
        ".pushsection .discard.reachable\n\t"
        ".long 998b\n\t"
        ".popsection\n\t"
        "call irq_exit_rcu\n\t"
        "popq %%rsp\n\t"
        : [tos] "+r"(tos), [sp] "+r"(sp)
        : [__func] "i"(irq_target1), [top] "r"(tos), [arg1] "r"(arg1)
        : STACK_CLOBBERS);
}

void test_irq2(void *stack_top, void *arg1, unsigned long arg2) {
    register void *tos asm("r11") = stack_top;
    __asm__ __volatile__(
        "movq %%rsp, (%[tos])\n\t"
        "movq %[tos], %%rsp\n\t"
        "call irq_enter_rcu\n\t"
        "movq %[arg2], %%rsi\n\t"
        "movq %[arg1], %%rdi\n\t"
        "call %c[__func]\n\t"
        "998:\n\t"
        ".pushsection .discard.reachable\n\t"
        ".long 998b\n\t"
        ".popsection\n\t"
        "call irq_exit_rcu\n\t"
        "popq %%rsp\n\t"
        : [tos] "+r"(tos), [sp] "+r"(sp)
        : [__func] "i"(irq_target2), [top] "r"(tos),
          [arg1] "r"(arg1), [arg2] "r"(arg2)
        : STACK_CLOBBERS);
}

void test_softirq(void *stack_top) {
    register void *tos asm("r11") = stack_top;
    __asm__ __volatile__(
        "movq %%rsp, (%[tos])\n\t"
        "movq %[tos], %%rsp\n\t"
        "call %c[__func]\n\t"
        "998:\n\t"
        ".pushsection .discard.reachable\n\t"
        ".long 998b\n\t"
        ".popsection\n\t"
        "popq %%rsp\n\t"
        : [tos] "+r"(tos), [sp] "+r"(sp)
        : [__func] "i"(softirq_target), [top] "r"(tos)
        : STACK_CLOBBERS);
}
#endif
