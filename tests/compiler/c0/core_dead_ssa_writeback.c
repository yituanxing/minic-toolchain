/* These arithmetic expressions are evaluated according to Core semantics,
 * but their scalar results are never consumed. No backend should force a
 * machine-frame writeback solely to preserve a dead SSA value. */
long dead_add(long x, long y) {
    (void)(x + y);
    (void)(x ^ 17L);
    return x;
}

volatile long observed;
long live_store(long x) {
    long y = x + 9;
    observed = y;
    return y;
}
