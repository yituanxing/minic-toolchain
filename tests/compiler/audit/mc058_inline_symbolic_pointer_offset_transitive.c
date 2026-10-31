/* MC-058 negative: the single static pointer fact crosses one parameter and
 * a constant element offset; GNU accepts, MiniC transitive=0 rejects. */
static int key_slots[4] = {3, 5, 7, 11};
static inline __attribute__((always_inline)) int leaf_key(int *key) {
    __asm__ __volatile__(".byte 0" : : "i"(key));
    return 1;
}
static inline __attribute__((always_inline)) int mid_key(int *key) {
    return leaf_key(key + 1);
}
int check_key(void) { return mid_key(key_slots); }
