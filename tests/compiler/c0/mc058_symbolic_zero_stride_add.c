/* GNU zero-sized complete records have a zero-byte pointer stride. Their
 * `base+1` symbolic immediate must remain the unmodified symbol, while
 * the existing &empty_array[1] subscript safety gate stays separate. */
struct Mc058Empty { };
static struct Mc058Empty mc058_empty_objects[2];
static inline __attribute__((always_inline)) int mc058_empty_immediate(
    struct Mc058Empty *p) {
    __asm__ volatile(".quad %c0" : : "i"(p));
    return 1;
}
int mc058_empty_probe(void) {
    return mc058_empty_immediate(mc058_empty_objects + 1);
}
