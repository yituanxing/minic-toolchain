/* The four GNU larchintrin.h extern-inline wrappers used by Linux 6.6.143.
 * Their bodies belong to genuine GNU machine builtins, never ELF libcalls.
 * Keep the spellings/signatures aligned with the frozen original GNU .i. */
extern __inline unsigned long
__attribute__((__gnu_inline__, __always_inline__, __artificial__))
__iocsrrd_d(unsigned int addr) {
    return (unsigned long)__builtin_loongarch_iocsrrd_d(addr);
}

extern __inline unsigned int
__attribute__((__gnu_inline__, __always_inline__, __artificial__))
__iocsrrd_w(unsigned int addr) {
    return (unsigned int)__builtin_loongarch_iocsrrd_w(addr);
}

extern __inline void
__attribute__((__gnu_inline__, __always_inline__, __artificial__))
__iocsrwr_d(unsigned long value, unsigned int addr) {
    __builtin_loongarch_iocsrwr_d(value, addr);
}

extern __inline void
__attribute__((__gnu_inline__, __always_inline__, __artificial__))
__iocsrwr_w(unsigned int value, unsigned int addr) {
    __builtin_loongarch_iocsrwr_w(value, addr);
}

unsigned long minic_iocsr_read64(unsigned int addr) {
    return __iocsrrd_d(addr);
}

unsigned int minic_iocsr_read32(unsigned int addr) {
    return __iocsrrd_w(addr);
}

void minic_iocsr_write64(unsigned long value, unsigned int addr) {
    __iocsrwr_d(value, addr);
}

void minic_iocsr_write32(unsigned int value, unsigned int addr) {
    __iocsrwr_w(value, addr);
}
