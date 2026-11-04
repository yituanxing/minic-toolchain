/* MC-058: the entry fact x=1 is invalid after x=y. A GNU `i` immediate
 * cannot be manufactured from the former value when y is runtime data.
 * GCC rejects this after inlining with "impossible constraint". */
__attribute__((always_inline)) static inline void leaf(int x, int y) {
    x = y;
    __asm__ volatile(".byte %0" : : "i"(x));
}

void probe(int y) { leaf(1, y); }
