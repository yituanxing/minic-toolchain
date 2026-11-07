/* Linux 6.6.143 arch/loongarch/include/asm/loongarch.h expands
 * read_cpucfg(reg) to the GCC <larchintrin.h> intrinsic __cpucfg(reg).
 * It is a machine intrinsic, never an ELF function reference. */
extern __inline unsigned int
__attribute__((__gnu_inline__, __always_inline__, __artificial__))
__cpucfg(unsigned int index) {
    return (unsigned int)__builtin_loongarch_cpucfg(index);
}

unsigned int linux_cpucfg_leaf(unsigned int index) {
    return __cpucfg(index);
}

unsigned int linux_cpucfg_zero(void) {
    return __cpucfg(0U);
}

unsigned int loong_cpucfg_existing_builtin(unsigned int index) {
    return __builtin_loongarch_cpucfg(index);
}
