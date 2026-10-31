/* Linux 6.6 arm-smmu-v3 dma_wmb(): a real Outer-Shareable ST barrier
 * following a variable-sized stack frame. It must not be elided or lowered
 * into a compiler-only fence when SP is dynamically adjusted. */
unsigned long smmu_dynamic_barrier(unsigned long count,
                                   const unsigned long *source) {
    volatile unsigned long *buffer =
        (volatile unsigned long *)__builtin_alloca(count * sizeof(*buffer));
    unsigned long i;
    for (i = 0; i < count; ++i) buffer[i] = source[i];
    __asm__ __volatile__("dmb oshst" ::: "memory");
    return count ? buffer[count - 1U] : 0U;
}

unsigned long smmu_static_barrier(unsigned long value) {
    __asm__ __volatile__("dmb oshst" ::: "memory");
    return value + 1UL;
}

#ifdef MINIC_DMB_BAD_INPUT
void smmu_bad_spurious_input(unsigned long count) {
    volatile unsigned long *buffer =
        (volatile unsigned long *)__builtin_alloca(count * sizeof(*buffer));
    /* The real hardware opcode has no operand placeholder. Do not accept
     * a structured source that invents an unused GNU register operand. */
    __asm__ __volatile__("dmb oshst" : : "r"(count) : "memory");
    if (count) buffer[0] = count;
}
#endif
