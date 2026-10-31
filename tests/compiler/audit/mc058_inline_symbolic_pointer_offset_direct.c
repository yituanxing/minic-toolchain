/* MC-058 positive control: exact same leaf as transitive case, direct fact. */
static int key_slots[4] = {3, 5, 7, 11};
static inline __attribute__((always_inline)) int leaf_key(int *key) {
    __asm__ __volatile__(".byte 0" : : "i"(key));
    return 1;
}
int check_key(void) { return leaf_key(key_slots + 1); }
