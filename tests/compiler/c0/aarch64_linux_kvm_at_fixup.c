/* Original Linux 6.6.143 arch/arm64/kvm/hyp/vhe/switch.c AT S1E1R.
 * The hyp exception-table entry and EL2 exception-state restoration are
 * inseparable from the AT operation; this fixture keeps all five GNU roles. */
typedef unsigned long u64;

__attribute__((noinline)) int translate_far(u64 far) {
    int err = 0;
    u64 spsr, elr;
    asm volatile(
        "mrs %1, spsr_el2\n"
        "mrs %2, elr_el2\n"
        "1: at s1e1r, %3\n"
        "isb\n"
        "b 9f\n"
        "2: msr spsr_el2, %1\n"
        "msr elr_el2, %2\n"
        "mov %w0, %4\n"
        "9: .pushsection __kvm_ex_table, \"a\"\n"
        ".align 3\n"
        ".long (1b - .), (2b - .)\n"
        ".popsection\n"
        : "+r"(err), "=&r"(spsr), "=&r"(elr)
        : "r"(far), "i"(-14));
    return err;
}
