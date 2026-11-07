/* Linux debugreg.h register family plus head64 data-segment reload.
 * Privileged instructions are compiled and assembled, never executed. */

#define DR_READ(number, name)                                    \
    unsigned long read_##name(void) {                            \
        unsigned long result;                                    \
        __asm__("mov %%db" #number ",%0" : "=r"(result));         \
        return result;                                           \
    }

#define DR_WRITE(number, name)                                   \
    void write_##name(unsigned long value) {                     \
        __asm__("mov %0,%%db" #number : : "r"(value));            \
    }

DR_READ(0, dr0)
DR_READ(1, dr1)
DR_READ(2, dr2)
DR_READ(3, dr3)
DR_READ(6, dr6)
DR_WRITE(0, dr0)
DR_WRITE(1, dr1)
DR_WRITE(2, dr2)
DR_WRITE(3, dr3)
DR_WRITE(6, dr6)

/* DR7's fake memory input must never be physically dereferenced. */
unsigned long read_dr7(void) {
    unsigned long result;
    __asm__ __volatile__("mov %%db7,%0" : "=r"(result) :
                         "m"(*(unsigned int *)0x1000UL));
    return result;
}

void write_dr7(unsigned long value) {
    __asm__ __volatile__("mov %0,%%db7" : : "r"(value),
                         "m"(*(unsigned int *)0x1000UL));
}

void linux_boot_segments(unsigned int selector) {
    __asm__ __volatile__(
        "movl %%eax, %%ds\n\t"
        "movl %%eax, %%ss\n\t"
        "movl %%eax, %%es"
        : : "a"(selector) : "memory");
}
