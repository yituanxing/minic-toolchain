/* Three qualified GNU x86 families with a shared observable scalar/memory
 * result. Volatile enforces the MMIO contract without requiring real devices. */
#if defined(GNU_ORACLE)
#define ENTRY gnu_typed_mmio_exchange
#else
#define ENTRY mini_typed_mmio_exchange
#endif

void ENTRY(unsigned int *word, unsigned int value,
           unsigned int *readback, unsigned int *old) {
    unsigned int from_memory;
    unsigned int exchanged = value + 1U;
    __asm__ __volatile__("movl %1, %0" : "=r" (from_memory)
                         : "m" (*word) : "memory");
    *readback = from_memory;
    __asm__ __volatile__("movl %0, %1" : : "r" (value), "m" (*word)
                         : "memory");
    __asm__ __volatile__("xchgl %0, %1" : "+r" (exchanged), "+m" (*word)
                         : : "memory", "cc");
    *old = exchanged;
}
