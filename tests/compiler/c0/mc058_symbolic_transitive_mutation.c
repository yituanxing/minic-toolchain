/* MC-058: a pointer parameter reassigned inside an inline caller must not
 * propagate its stale entry symbol into a transitive leaf's GNU `i` operand.
 * GCC emits mc058_rebound_new; a conservative MiniC rejection is safe. */
static int mc058_rebound_old;
static int mc058_rebound_new;

__attribute__((always_inline)) static inline int mc058_rebound_leaf(int *p) {
    __asm__ volatile(".quad %c0" : : "i"(p));
    return 1;
}
__attribute__((always_inline)) static inline int mc058_rebound_middle(int *p) {
    p = &mc058_rebound_new;
    return mc058_rebound_leaf(p);
}
int mc058_rebound_entry(void) {
    return mc058_rebound_middle(&mc058_rebound_old);
}
