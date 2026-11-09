/* MC-058: 2^61 int elements require a byte addend of 2^63, which cannot
 * fit signed 64-bit ELF relocation arithmetic. Fail closed; never wrap the
 * product to INT64_MIN or emit a misleading symbol+offset immediate. */
static int mc058_overflow_words[2];
static inline __attribute__((always_inline)) int mc058_overflow_asm(int *p) {
    __asm__ volatile(".quad %c0" : : "i"(p));
    return 1;
}
int mc058_overflow_probe(void) {
    return mc058_overflow_asm(mc058_overflow_words + 0x2000000000000000LL);
}
