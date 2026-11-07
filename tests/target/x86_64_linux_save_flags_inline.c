/* Linux 6.6.143 arch/x86/include/asm/irqflags.h native_save_fl(). */
extern inline __attribute__((__gnu_inline__, __always_inline__))
unsigned long native_save_fl(void) {
    unsigned long flags;
    asm volatile("# __raw_save_flags\n\tpushf ; pop %0"
                 : "=rm"(flags) : : "memory");
    return flags;
}

static inline __attribute__((__always_inline__))
unsigned long arch_local_save_flags(void) {
    return native_save_fl();
}

unsigned long sample_flags(void) {
    return arch_local_save_flags();
}
