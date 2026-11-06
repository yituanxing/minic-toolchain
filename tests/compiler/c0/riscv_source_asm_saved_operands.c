/* The original RISC-V Linux irq.c runs inline ASM that clobbers the whole
 * caller-saved a/t bank. GCC binds its remaining generic inputs to saved
 * s-registers; the MiniC RV64 frame must save/restore exactly those. */
#if defined(MINIC_BAD_NO_AVAILABLE_GPR)
void cannot_bind_operand(unsigned long first) {
    __asm__ volatile("xor zero, %0, %0" : : "r"(first) :
        "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
        "t0", "t1", "t2", "t3", "t4", "t5", "t6",
        "s0", "s1", "s2", "s3", "s4", "s5", "s6",
        "s7", "s8", "s9", "s10", "s11", "memory");
}
#else
unsigned long linux_irq_two_saved_inputs(unsigned long first,
                                          unsigned long second) {
    __asm__ volatile("xor zero, %[first], %[second]" :
        : [first] "r"(first), [second] "r"(second) :
        "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
        "t0", "t1", "t2", "t3", "t4", "t5", "t6", "memory");
    return first + second;
}
#endif
