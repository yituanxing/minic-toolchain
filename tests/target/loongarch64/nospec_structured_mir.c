/* Exact Linux array_index_mask_nospec source-owned two-opcode template. */
unsigned long loong_nospec_mir(unsigned long index, unsigned long size) {
    unsigned long mask;
    __asm__ __volatile__(
        "sltu\t%0, %1, %2\n\t"
        "sub.d\t%0, $zero, %0\n\t"
        : "=r"(mask) : "r"(index), "r"(size) :);
    return mask;
}
