/* MC-058 positive control: after a known write the live value is 2,
 * not the entry-specialized parameter value 1. The emitted byte is 2. */
__attribute__((always_inline)) static inline void leaf(int x) {
    x = 2;
    __asm__ volatile(".byte %0" : : "i"(x));
}

void probe(void) { leaf(1); }
