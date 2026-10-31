/* Original Linux x86 6.6 selector-snapshot and idle hardware contracts.
 * Compile and inspect only: privileged instructions must never be run. */
struct linux_thread_selectors {
    unsigned short es, ds;
};

void linux_copy_thread_segments(struct linux_thread_selectors *thread) {
    asm("mov %%es,%0" : "=r" (thread->es) : : "memory");
    asm("mov %%ds,%0" : "=r" (thread->ds) : : "memory");
}

void linux_monitor(const void *address, unsigned long extensions,
                   unsigned long hints) {
    asm volatile(".byte 0x0f, 0x01, 0xc8;"
                 : : "a" (address), "c" (extensions), "d" (hints));
}

void linux_monitorx(const void *address, unsigned long extensions,
                    unsigned long hints) {
    asm volatile(".byte 0x0f, 0x01, 0xfa;"
                 : : "a" (address), "c" (extensions), "d" (hints));
}

void linux_mwait(unsigned long eax, unsigned long ecx) {
    asm volatile(".byte 0x0f, 0x01, 0xc9;" : : "a" (eax), "c" (ecx));
}

void linux_mwaitx(unsigned long eax, unsigned long ebx,
                 unsigned long ecx) {
    asm volatile(".byte 0x0f, 0x01, 0xfb;"
                 : : "a" (eax), "b" (ebx), "c" (ecx));
}

void linux_sti_mwait(unsigned long eax, unsigned long ecx) {
    asm volatile("sti; .byte 0x0f, 0x01, 0xc9;"
                 : : "a" (eax), "c" (ecx));
}
