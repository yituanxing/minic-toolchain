/* MC-058: an incoming specialization range belongs to a parameter VALUE,
 * not to its mutable storage across writes, branches, and address escapes.
 * GCC and MiniC must both return zero from main on x86 native execution.
 * All five target compilers must accept this same GNU-preprocessed fixture. */
__attribute__((always_inline)) static inline int leaf_range(int x) {
    return x == 2 ? 1 : 0;
}

__attribute__((always_inline)) static inline int middle_assign(int x) {
    x = 2;
    return leaf_range(x);
}

__attribute__((always_inline)) static inline int middle_increment(int x) {
    ++x;
    return leaf_range(x);
}

__attribute__((always_inline)) static inline int middle_conditional(int x, int change) {
    if (change)
        x = 2;
    return leaf_range(x);
}

__attribute__((always_inline)) static inline int middle_escape(int x) {
    int *alias = &x;
    *alias = 2;
    return leaf_range(x);
}

__attribute__((always_inline)) static inline int middle_loop(int x) {
    int n = 0;
    while (n < 1) {
        x = 2;
        ++n;
    }
    return leaf_range(x);
}

int check_assign(int z) { return middle_assign((_Bool)z); }
int check_increment(int z) { return middle_increment((_Bool)z); }
int check_conditional(int z, int change) {
    return middle_conditional((_Bool)z, change);
}
int check_escape(int z) { return middle_escape((_Bool)z); }
int check_loop(int z) { return middle_loop((_Bool)z); }

int main(void) {
    return check_assign(0) != 1 || check_assign(1) != 1 ||
           check_increment(0) != 0 || check_increment(1) != 1 ||
           check_conditional(0, 0) != 0 || check_conditional(1, 0) != 0 ||
           check_conditional(0, 1) != 1 || check_conditional(1, 1) != 1 ||
           check_escape(0) != 1 || check_escape(1) != 1 ||
           check_loop(0) != 1 || check_loop(1) != 1;
}
