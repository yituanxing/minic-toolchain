/* MC-058: the ingress constant 1 is killed by x=y. GNU rejects its later
 * `i` constraint because y is an arbitrary runtime value; MiniC must not
 * silently emit the stale `.byte 1` from clone entrance facts. */
static inline __attribute__((always_inline)) int mc058_rebound(int x, int y) {
    x = y;
    __asm__ volatile(".byte %c0" : : "i"(x));
    return 1;
}
int mc058_rebound_probe(int y) { return mc058_rebound(1, y); }
