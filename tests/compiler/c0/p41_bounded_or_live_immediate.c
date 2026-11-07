/* P41: a specialization fact describes the function entry, not a parameter
 * after a compound assignment. Even when a bounded-bit analysis retains
 * useful information, the old immediate fact must be invalidated. */
__attribute__((always_inline)) static inline int p41_or_live(int x) {
    x |= 2;
    __asm__ volatile(".byte %c0" : : "i"(x));
    return x;
}

int p41_or_live_probe(void) { return p41_or_live(0); }

extern int p41_unknown(int);
__attribute__((always_inline)) static inline int p41_or_live_barrier(int x, int y) {
    x |= 2;
    if (y) p41_unknown(x);
    __asm__ volatile(".byte %c0" : : "i"(x));
    return x;
}
int p41_or_barrier_probe(int z) { return p41_or_live_barrier(0, z); }
