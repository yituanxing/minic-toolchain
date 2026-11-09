/* Original Linux 6.6 arm64 atomic_ll_sc.h __CMPXCHG_CASE: width and
 * memory-order combinations, including the required narrow old cast. */
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

#define NO_CLOBBER
#define MEMORY_CLOBBER : "memory"
#define DEFINE_CMPXCHG(T, W, MODE, LD, ST, FENCE, CLOB)                  \
T cmp_##W##_##MODE(volatile void *ptr, unsigned long old, T replacement) { \
    unsigned long tmp;                                                   \
    T oldval;                                                            \
    old = (T)old;                                                        \
    __asm__ __volatile__(                                               \
        " prfm pstl1strm, %[v]\n"                                      \
        "1: " #LD " %w[oldval], %[v]\n"                                \
        " eor %w[tmp], %w[oldval], %w[old]\n"                          \
        " cbnz %w[tmp], 2f\n"                                           \
        " " #ST " %w[tmp], %w[new], %[v]\n"                            \
        " cbnz %w[tmp], 1b\n"                                           \
        FENCE "2:"                                                     \
        : [tmp] "=&r" (tmp), [oldval] "=&r" (oldval),                    \
          [v] "+Q" (*(T *)ptr)                                         \
        : [old] "Kr" (old), [new] "r" (replacement) CLOB);              \
    return oldval;                                                       \
}

DEFINE_CMPXCHG(u8, 8, relaxed, ldxrb, stxrb, "", NO_CLOBBER)
DEFINE_CMPXCHG(u8, 8, acquire, ldaxrb, stxrb, "", MEMORY_CLOBBER)
DEFINE_CMPXCHG(u8, 8, release, ldxrb, stlxrb, "", MEMORY_CLOBBER)
DEFINE_CMPXCHG(u8, 8, full, ldxrb, stlxrb, " dmb ish\n", MEMORY_CLOBBER)
DEFINE_CMPXCHG(u16, 16, relaxed, ldxrh, stxrh, "", NO_CLOBBER)
DEFINE_CMPXCHG(u16, 16, acquire, ldaxrh, stxrh, "", MEMORY_CLOBBER)
DEFINE_CMPXCHG(u16, 16, release, ldxrh, stlxrh, "", MEMORY_CLOBBER)
DEFINE_CMPXCHG(u16, 16, full, ldxrh, stlxrh, " dmb ish\n", MEMORY_CLOBBER)

/* Exercise the 32/64-bit members of the *same* 16-form ISA contract,
 * including GNU register width and Lr-versus-Kr old-value constraints.
 * Width=64 uses X registers, while subword/word values use W registers;
 * the STXR/STLXR status remains W for every width. */
#define DEFINE_WIDE_CMPXCHG(T, W, REG, CON, MODE, LD, ST, FENCE, CLOB)    \
T cmp_##W##_##MODE(volatile void *ptr, unsigned long old, T replacement) { \
    unsigned long tmp;                                                     \
    T oldval;                                                              \
    old = (T)old;                                                          \
    __asm__ __volatile__(                                                 \
        " prfm pstl1strm, %[v]\n"                                        \
        "1: " #LD " " REG "[oldval], %[v]\n"                             \
        " eor " REG "[tmp], " REG "[oldval], " REG "[old]\n"              \
        " cbnz " REG "[tmp], 2f\n"                                       \
        " " #ST " %w[tmp], " REG "[new], %[v]\n"                          \
        " cbnz %w[tmp], 1b\n"                                            \
        FENCE "2:"                                                        \
        : [tmp] "=&r" (tmp), [oldval] "=&r" (oldval),                      \
          [v] "+Q" (*(T *)ptr)                                           \
        : [old] CON (old), [new] "r" (replacement) CLOB);                \
    return oldval;                                                         \
}

DEFINE_WIDE_CMPXCHG(u32, 32, "%w", "Kr", relaxed, ldxr, stxr, "", NO_CLOBBER)
DEFINE_WIDE_CMPXCHG(u32, 32, "%w", "Kr", acquire, ldaxr, stxr, "", MEMORY_CLOBBER)
DEFINE_WIDE_CMPXCHG(u32, 32, "%w", "Kr", release, ldxr, stlxr, "", MEMORY_CLOBBER)
DEFINE_WIDE_CMPXCHG(u32, 32, "%w", "Kr", full, ldxr, stlxr, " dmb ish\n", MEMORY_CLOBBER)
DEFINE_WIDE_CMPXCHG(u64, 64, "%", "Lr", relaxed, ldxr, stxr, "", NO_CLOBBER)
DEFINE_WIDE_CMPXCHG(u64, 64, "%", "Lr", acquire, ldaxr, stxr, "", MEMORY_CLOBBER)
DEFINE_WIDE_CMPXCHG(u64, 64, "%", "Lr", release, ldxr, stlxr, "", MEMORY_CLOBBER)
DEFINE_WIDE_CMPXCHG(u64, 64, "%", "Lr", full, ldxr, stlxr, " dmb ish\n", MEMORY_CLOBBER)

int test_narrow_byte(void) {
    u8 v = 3;
    /* Every success must publish exactly its replacement and expose the
     * previous value, while the mismatch branch leaves memory unchanged. */
    u8 a = cmp_8_relaxed(&v, 0x103UL, 4);
    u8 b = cmp_8_acquire(&v, 0x204UL, 5);
    u8 c = cmp_8_release(&v, 0x206UL, 9);
    u8 d = cmp_8_full(&v, 0x105UL, 6);
    return v == 6 && a == 3 && b == 4 && c == 5 && d == 5;
}

int test_narrow_half(void) {
    u16 v = 0x1234;
    u16 a = cmp_16_relaxed(&v, 0x11234UL, 0x2345);
    u16 b = cmp_16_acquire(&v, 0x22345UL, 0x3456);
    u16 c = cmp_16_release(&v, 0x13457UL, 0x4444);
    u16 d = cmp_16_full(&v, 0x13456UL, 0x4567);
    return v == 0x4567 && a == 0x1234 && b == 0x2345 &&
           c == 0x3456 && d == 0x3456;
}

int test_wide_word(void) {
    u32 v = 0x80001234U;
    u32 a = cmp_32_relaxed(&v, 0x180001234UL, 0x01010101U);
    u32 b = cmp_32_acquire(&v, 0x101010101UL, 0x02020202U);
    u32 c = cmp_32_release(&v, 0x102020203UL, 0x04040404U);
    u32 d = cmp_32_full(&v, 0x102020202UL, 0x03030303U);
    return v == 0x03030303U && a == 0x80001234U &&
           b == 0x01010101U && c == 0x02020202U && d == 0x02020202U;
}

int test_wide_double(void) {
    u64 v = 0x1234567887654321ULL;
    u64 a = cmp_64_relaxed(&v, 0x1234567887654321UL, 0x1020304050607080ULL);
    u64 b = cmp_64_acquire(&v, 0x1020304050607080UL, 0x2233445566778899ULL);
    u64 c = cmp_64_release(&v, 0x2233445566778898UL, 0x9988776655443322ULL);
    u64 d = cmp_64_full(&v, 0x2233445566778899UL, 0xaabbccddeeff0011ULL);
    return v == 0xaabbccddeeff0011ULL && a == 0x1234567887654321ULL &&
           b == 0x1020304050607080ULL && c == 0x2233445566778899ULL &&
           d == 0x2233445566778899ULL;
}

int main(void) {
    return test_narrow_byte() && test_narrow_half() &&
           test_wide_word() && test_wide_double() ? 42 : 1;
}
