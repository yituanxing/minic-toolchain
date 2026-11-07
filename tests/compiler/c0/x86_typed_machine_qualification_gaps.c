/* x86 target-owned GNU MXCSR and ORC-unwind qualification regression. */
typedef unsigned int u32;
typedef unsigned long u64;

#if defined(NEGATIVE_MXCSR_REGISTER)
void probe(u32 *value) {
    u32 result;
    __asm__ __volatile__("stmxcsr %0" : "=r" (result));
    *value = result;
}
#elif defined(NEGATIVE_MXCSR_WIDTH)
void probe(u64 *value) {
    __asm__ __volatile__("stmxcsr %0" : "=m" (*value));
}
#elif defined(NEGATIVE_MXCSR_CLOBBER)
void probe(u32 *value) {
    __asm__ __volatile__("stmxcsr %0" : "=m" (*value) : : "cc");
}
#elif defined(NEGATIVE_MXCSR_SUFFIX)
void probe(u32 *value) {
    __asm__ __volatile__("stmxcsr %0; cli" : "=m" (*value));
}
#elif defined(NEGATIVE_UNWIND_MEMORY)
void probe(u64 *a, u64 *b, u64 *c) {
    __asm__ __volatile__("lea (%%rip), %0\n\tmov %%rsp, %1\n\tmov %%rbp, %2"
                         : "=m" (*a), "=r" (*b), "=r" (*c));
}
#elif defined(NEGATIVE_UNWIND_CLOBBER)
void probe(u64 *a, u64 *b, u64 *c) {
    __asm__ __volatile__("lea (%%rip), %0\n\tmov %%rsp, %1\n\tmov %%rbp, %2"
                         : "=r" (*a), "=r" (*b), "=r" (*c) : : "memory");
}
#elif defined(NEGATIVE_UNWIND_SUFFIX)
void probe(u64 *a, u64 *b, u64 *c) {
    __asm__ __volatile__("lea (%%rip), %0\n\tmov %%rsp, %1\n\tmov %%rbp, %2\n\tcli"
                         : "=r" (*a), "=r" (*b), "=r" (*c));
}
#else
void probe_mxcsr_store(u32 *value) {
    __asm__ __volatile__("stmxcsr %0" : "=m" (*value));
}
void probe_mxcsr_load(const u32 *value) {
    __asm__ __volatile__("ldmxcsr %0" : : "m" (*value));
}
void probe_mxcsr_store_named(u32 *value) {
    __asm__ __volatile__("stmxcsr %[val]" : [val] "=m" (*value));
}
void probe_unwind(u64 *a, u64 *b, u64 *c) {
    __asm__ __volatile__("lea (%%rip), %0\n\tmov %%rsp, %1\n\tmov %%rbp, %2"
                         : "=r" (*a), "=r" (*b), "=r" (*c));
}
#endif
