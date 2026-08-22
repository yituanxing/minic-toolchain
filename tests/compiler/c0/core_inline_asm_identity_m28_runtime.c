extern unsigned long core_m28_inline_asm_identity(unsigned long);
extern unsigned int core_m28_iter_div_u64_rem(unsigned long, unsigned int, unsigned long *);

int main(void) {
    unsigned long remainder = 0;
    if (core_m28_inline_asm_identity(123UL) != 123UL) return 1;
    if (core_m28_iter_div_u64_rem(23UL, 5U, &remainder) != 4U || remainder != 3UL) return 2;
    remainder = 0;
    if (core_m28_iter_div_u64_rem(3UL, 5U, &remainder) != 0U || remainder != 3UL) return 3;
    return 0;
}
