/* MC-058: the same source FunctionBody is called through one and two
 * symbolic-pointer specialization levels. The Core selector must compare
 * normalized symbol+byte-addend, not separately allocated ExpressionIds. */
static int mc058_chain_slots[16];

__attribute__((always_inline)) static inline int mc058_chain_leaf(int *p) {
    __asm__ volatile(".quad %c0" : : "i"(p));
    return 1;
}
__attribute__((always_inline)) static inline int mc058_chain_plus(int *p) {
    return mc058_chain_leaf(p + 1);
}
__attribute__((always_inline)) static inline int mc058_chain_commuted(int *p) {
    return mc058_chain_leaf(1 + p);
}
__attribute__((always_inline)) static inline int mc058_chain_minus(int *p) {
    return mc058_chain_leaf(p - 1);
}
__attribute__((always_inline)) static inline int mc058_chain_two_hops(int *p) {
    return mc058_chain_plus(p + 1);
}

int mc058_chain_entry_plus(void) { return mc058_chain_plus(mc058_chain_slots); }
int mc058_chain_entry_commuted(void) {
    return mc058_chain_commuted(mc058_chain_slots);
}
int mc058_chain_entry_minus(void) {
    return mc058_chain_minus(mc058_chain_slots + 2);
}
int mc058_chain_entry_two_hops(void) {
    return mc058_chain_two_hops(mc058_chain_slots);
}
