/* P41: a one-sided bounded-bit proof survives OR, but a speculative inline
 * parameter *entry* fact must not survive conditional, nested, or loop writes.
 * The source parameter is constant at the callsite, the control flag is not. */
__attribute__((always_inline)) static inline int p41_or_cond(int x, int y) {
    if (y) x |= 2;
    return x;
}

__attribute__((always_inline)) static inline int p41_or_nested(int x, int y) {
    if (y) {
        x |= 2;
        if (y > 2) x |= 4;
    }
    return x;
}

__attribute__((always_inline)) static inline int p41_or_loop(int x, int y) {
    int i = 0;
    while (i < y) {
        x |= 2;
        ++i;
    }
    return x;
}

int main(int argc, char **argv) {
    int y = argc - 1;
    int expected_nested;
    (void)argv;
    expected_nested = y <= 0 ? 1 : y <= 2 ? 3 : 7;
    return p41_or_cond(0, y) != (y ? 2 : 0) ||
           p41_or_cond(1, y) != (y ? 3 : 1) ||
           p41_or_nested(1, y) != expected_nested ||
           p41_or_loop(0, y) != (y > 0 ? 2 : 0);
}
