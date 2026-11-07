/* MC-058: a cloned entry pointer fact must not outlive assignment to its
 * parameter local. GNU resolves the immediate to the newly assigned object;
 * MiniC must either resolve it to that object or reject conservatively,
 * NEVER substitute the stale entry symbol. */
static int mc058_old_target;
static int mc058_new_target;
static inline __attribute__((always_inline)) int mc058_rebind(int *p) {
    p = &mc058_new_target;
    __asm__ volatile(".quad %c0" : : "i"(p));
    return 1;
}
int mc058_rebind_probe(void) { return mc058_rebind(&mc058_old_target); }
