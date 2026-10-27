/* Original Linux 6.6.143 AArch64 array_index_mask_nospec statement.
 * Minimal TU for exactly qualified NZCV cmp/sbc, GNU AS and QEMU. */
unsigned long array_index_mask_nospec(unsigned long idx, unsigned long sz) {
    unsigned long mask;
    asm volatile("cmp %1, %2\n\tsbc %0, xzr, xzr\n"
                 : "=r"(mask) : "r"(idx), "Ir"(sz) : "cc");
    return mask;
}
int main(void) {
    if (array_index_mask_nospec(3UL, 4UL) != ~0UL) return 1;
    if (array_index_mask_nospec(4UL, 4UL) != 0UL) return 2;
    if (array_index_mask_nospec(5UL, 4UL) != 0UL) return 3;
    if (array_index_mask_nospec(0UL, 0UL) != 0UL) return 4;
    if (array_index_mask_nospec(0UL, 1UL) != ~0UL) return 5;
    return 42;
}
