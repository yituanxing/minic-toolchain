/* A volatile parameter access is a fresh C read, even when its caller
 * supplied an integer constant. Never replace it with a clone entrance
 * GNU `i` immediate; GCC rejects the impossible constraint. */
static inline __attribute__((always_inline)) int mc058_volatile(int volatile x) {
    __asm__ volatile(".byte %c0" : : "i"(x));
    return 1;
}
int mc058_volatile_probe(void) { return mc058_volatile(1); }
