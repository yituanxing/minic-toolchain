/* Original Linux 6.6.143 arch/arm64/include/asm/compiler.h XPACLRI
 * local-register idiom. This source deliberately preserves its GNU tied
 * +r x30 contract instead of substituting an ordinary register operand. */
#define ARM64_ASM_ARCH "armv8.3-a"
#define ARM64_ASM_PREAMBLE ".arch " ARM64_ASM_ARCH "\n"

#define xpaclri(ptr)                                                   \
({                                                                     \
    register unsigned long __xpaclri_ptr asm("x30") = (ptr);            \
    asm(ARM64_ASM_PREAMBLE "\thint\t#7\n" : "+r" (__xpaclri_ptr));     \
    __xpaclri_ptr;                                                      \
})

unsigned long minic_linux_xpaclri(unsigned long ptr) {
    return xpaclri(ptr);
}

unsigned long minic_linux_xpaclri_twice(unsigned long a, unsigned long b) {
    unsigned long first = xpaclri(a);
    return first ^ xpaclri(b);
}

/* ARM64_ASM_PREAMBLE is absent when Kbuild's assembler does not request an
 * explicit architecture directive: both original forms must remain valid. */
#undef ARM64_ASM_PREAMBLE
#define ARM64_ASM_PREAMBLE

unsigned long minic_linux_xpaclri_without_arch(unsigned long ptr) {
    return xpaclri(ptr);
}
