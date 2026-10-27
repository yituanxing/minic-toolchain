unsigned long array_index_mask_nospec(unsigned long idx, unsigned long sz) {
    unsigned long mask;
    asm volatile("cmp %1, %2\n\tsbc %0, xzr, xzr\n"
                 : "=r"(mask) : "r"(idx), "Ir"(sz) : "cc");
    return mask;
}
register unsigned long current_stack_pointer asm("sp");
static unsigned long read_live_sp(void) {
    volatile unsigned long local = 21UL;
    unsigned long snapshot = current_stack_pointer;
    unsigned long local_address = (unsigned long)&local;
    if (snapshot == 0UL || (snapshot & 15UL) != 0UL ||
        local_address < snapshot || local_address - snapshot > 4096UL)
        return 1UL;
    return local;
}
__attribute__((noinline))
static unsigned long inner_return_clobber(unsigned long value) {
    return value + 1UL;
}
__attribute__((noinline))
static void *read_saved_return_address(void) {
    if (inner_return_clobber(41UL) != 42UL) return (void *)0;
    return __builtin_return_address(0);
}
int main(void) {
    if (array_index_mask_nospec(3UL, 4UL) != ~0UL) return 1;
    if (array_index_mask_nospec(4UL, 4UL) != 0UL) return 2;
    if (array_index_mask_nospec(5UL, 4UL) != 0UL) return 3;
    if (array_index_mask_nospec(0UL, 0UL) != 0UL) return 4;
    if (array_index_mask_nospec(0UL, 1UL) != ~0UL) return 5;
    if (read_live_sp() != 21UL) return 6;
    void *first = read_saved_return_address();
    void *second = read_saved_return_address();
    if (!first || !second || first == second) return 7;
    return 42;
}
