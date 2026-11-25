/* Linux 6.6 KVM PMU direct filter read, original one-output GNU read_sysreg.
 * PMCCFILTR_EL0 is privileged hardware state, not a compiler-side value. */
unsigned long long minic_pmu_read_pmccfiltr(void) {
    unsigned long long value;
    asm volatile("mrs %0, pmccfiltr_el0" : "=r" (value));
    return value;
}
