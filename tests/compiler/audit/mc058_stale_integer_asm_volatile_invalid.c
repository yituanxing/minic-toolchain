/* MC-058: even an unmodified volatile parameter is not an integer constant
 * expression for a GNU `i` input. An entrance specialization must not erase
 * the volatile read or invent an immediate from the original argument. */
__attribute__((always_inline)) static inline void leaf(volatile int x) {
    __asm__ volatile(".byte %0" : : "i"(x));
}

void probe(void) { leaf(1); }
