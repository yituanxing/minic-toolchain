/* Real GNU function entry alignment is symbol/section layout, not a hint.
 * Model the original Linux kernel/exit.c weak aligned abort definition,
 * together with prefix, suffix and redeclared alignment contracts. */
__attribute__((aligned(64))) int aligned_prefix(int value) {
    return value + 1;
}

int aligned_suffix(int value) __attribute__((aligned(32)));
int aligned_suffix(int value) { return value * 2; }

extern __attribute__((aligned(128))) int aligned_declaration(int value);
int aligned_declaration(int value) { return value + 3; }

__attribute__((weak)) __attribute__((aligned(32)))
int linux_weak_aligned(int value) { return value + 4; }

/* A later compatible declaration preserves the same entry alignment. */
extern __attribute__((aligned(64))) int aligned_prefix(int value);

int main(void) {
    return (aligned_prefix(1) == 2 && aligned_suffix(2) == 4 &&
            aligned_declaration(3) == 6 && linux_weak_aligned(4) == 8)
               ? 0 : 1;
}
