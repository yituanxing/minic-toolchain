/* MC-058: a caller's entry integer fact stops at a parameter write.
 * A transitive clone of leaf(x) with x=7 would use stale source facts:
 * middle replaces x with 2 before the call. Preserve the live value and
 * avoid spending clone quota on an unreachable entry-value specialization. */
__attribute__((always_inline)) static inline int leaf(int x, int salt) {
    return x == 2 ? salt : 0;
}

__attribute__((always_inline)) static inline int middle(int x, int salt) {
    x = 2;
    return leaf(x, salt);
}

int verify(int salt) { return middle(7, salt); }
int main(void) { return verify(1) != 1 || verify(37) != 37; }
