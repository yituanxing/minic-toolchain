/* MC-001: transitive symbolic closure builds &global[index] using an
 * integer-specialized inline caller.  A recursive rewrite may grow the
 * Program's Expression arena while its parent has an active ExpressionId. */
#ifndef PAD_COUNT
#define PAD_COUNT 0
#endif

#define PAD_1(x) ((x) + 1)
#define PAD_2(x) PAD_1(PAD_1(x))
#define PAD_4(x) PAD_2(PAD_2(x))
#define PAD_8(x) PAD_4(PAD_4(x))
#define PAD_16(x) PAD_8(PAD_8(x))
#define PAD_32(x) PAD_16(PAD_16(x))
#define PAD_64(x) PAD_32(PAD_32(x))
#define PAD_128(x) PAD_64(PAD_64(x))

#if PAD_COUNT < 0 || PAD_COUNT > 255
#error PAD_COUNT must fit in eight bits
#endif

#if PAD_COUNT & 1
#define APPLY_1(x) PAD_1(x)
#else
#define APPLY_1(x) (x)
#endif
#if PAD_COUNT & 2
#define APPLY_2(x) PAD_2(x)
#else
#define APPLY_2(x) (x)
#endif
#if PAD_COUNT & 4
#define APPLY_4(x) PAD_4(x)
#else
#define APPLY_4(x) (x)
#endif
#if PAD_COUNT & 8
#define APPLY_8(x) PAD_8(x)
#else
#define APPLY_8(x) (x)
#endif
#if PAD_COUNT & 16
#define APPLY_16(x) PAD_16(x)
#else
#define APPLY_16(x) (x)
#endif
#if PAD_COUNT & 32
#define APPLY_32(x) PAD_32(x)
#else
#define APPLY_32(x) (x)
#endif
#if PAD_COUNT & 64
#define APPLY_64(x) PAD_64(x)
#else
#define APPLY_64(x) (x)
#endif
#if PAD_COUNT & 128
#define APPLY_128(x) PAD_128(x)
#else
#define APPLY_128(x) (x)
#endif

#define PAD_VALUE APPLY_128(APPLY_64(APPLY_32(APPLY_16(\
    APPLY_8(APPLY_4(APPLY_2(APPLY_1(1))))))))

static int pad_anchor(void) { return PAD_VALUE; }
static int globals[12] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };

#ifdef MC001_TWO_ARGUMENTS
__attribute__((always_inline)) static inline int leaf(int *left, int *right) {
    return *left + *right;
}

__attribute__((always_inline)) static inline int middle(int index, int offset) {
    return leaf(&globals[index], &globals[offset]);
}

int main(void) {
    return middle(2, 3) == 7 && pad_anchor() == PAD_COUNT + 1 ? 0 : 1;
}
#else
__attribute__((always_inline)) static inline int leaf(int *value) {
    return *value;
}

__attribute__((always_inline)) static inline int middle(int index) {
    return leaf(&globals[index]);
}

int main(void) {
    return middle(6) == 7 && pad_anchor() == PAD_COUNT + 1 ? 0 : 1;
}
#endif
