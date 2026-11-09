/* P41: dynamic path changes an entry-specialized parameter via bounded OR;
 * its value is no longer admissible for GNU `i` immediate operands. */
__attribute__((always_inline)) static inline int p41_or_conditional(int x, int y) {
    if (y) x |= 2;
    __asm__ volatile(".byte %c0" : : "i"(x));
    return x;
}
int p41_or_conditional_probe(int y) { return p41_or_conditional(0, y); }
