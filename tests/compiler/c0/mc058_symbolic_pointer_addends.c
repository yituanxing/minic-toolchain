/* MC-058: GNU asm `i` symbol addends are byte offsets, while source C
 * pointer +/- integer uses the pointee size. The %c modifier prints the
 * raw address on x86 GCC and in MiniC's common immediate normalizer. */
struct Mc058Pair { int lo; long long hi; };
static int mc058_words[8];
static char mc058_bytes[8];
static long long mc058_wides[8];
static struct Mc058Pair mc058_pairs[8];

static inline __attribute__((always_inline)) int mc058_int(int *p) {
    __asm__ volatile(".quad %c0" : : "i"(p));
    return 1;
}
static inline __attribute__((always_inline)) int mc058_char(char *p) {
    __asm__ volatile(".quad %c0" : : "i"(p));
    return 1;
}
static inline __attribute__((always_inline)) int mc058_wide(long long *p) {
    __asm__ volatile(".quad %c0" : : "i"(p));
    return 1;
}
static inline __attribute__((always_inline)) int mc058_pair(struct Mc058Pair *p) {
    __asm__ volatile(".quad %c0" : : "i"(p));
    return 1;
}
static inline __attribute__((always_inline)) int mc058_void(void *p) {
    __asm__ volatile(".quad %c0" : : "i"(p));
    return 1;
}

int mc058_int_plus(void) { return mc058_int(mc058_words + 1); }
int mc058_int_commuted(void) { return mc058_int(1 + mc058_words); }
int mc058_int_minus(void) { return mc058_int(mc058_words - 1); }
int mc058_char_plus(void) { return mc058_char(mc058_bytes + 1); }
int mc058_wide_plus(void) { return mc058_wide(mc058_wides + 1); }
int mc058_pair_plus(void) { return mc058_pair(mc058_pairs + 1); }
int mc058_void_plus(void) { return mc058_void((void *)mc058_words + 1); }
int mc058_cast_char_plus(void) { return mc058_char((char *)mc058_words + 1); }
