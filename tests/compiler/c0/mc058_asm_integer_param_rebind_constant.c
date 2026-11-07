/* A write kills the entry specialization fact, but local Core tracking may
 * still prove a newly assigned constant. Preserve that valid immediate. */
static inline __attribute__((always_inline)) int mc058_rebind_constant(int x) {
    x = 2;
    __asm__ volatile(".byte %c0" : : "i"(x));
    return x;
}
int mc058_rebind_constant_probe(void) { return mc058_rebind_constant(1); }
