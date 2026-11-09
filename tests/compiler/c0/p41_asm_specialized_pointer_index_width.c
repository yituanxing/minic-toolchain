/* A typed specialized index feeds the symbol+byte-addend normalizer.
 * After narrowing 258+1 to unsigned char, the int[] offset is 3*4=12,
 * never 259*4 or 259 bytes. */
int p41_address_slots[64];
__attribute__((always_inline)) static inline int p41_address_leaf(unsigned int index) {
    __asm__ volatile(".quad %c0" ::
                     "i"(&p41_address_slots[(unsigned char)(index + 1U)]));
    return 0;
}
int p41_address_entry(void) { return p41_address_leaf(258U); }
