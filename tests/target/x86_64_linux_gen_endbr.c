/* Linux 6.6.143 arch/x86/include/asm/ibt.h gen_endbr GNU source grammar. */
typedef unsigned int u32;
static inline __attribute__((__gnu_inline__,__always_inline__))
u32 gen_endbr(void) {
    u32 endbr;
    asm ("mov $~0xfa1e0ff3, %[endbr]\n\t"
         "not %[endbr]\n\t" : [endbr] "=&r"(endbr));
    return endbr;
}

u32 sample_endbr(void) {
    return gen_endbr();
}
