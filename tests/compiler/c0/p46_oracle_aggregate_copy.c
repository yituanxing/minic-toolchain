/* P46: compare complete aggregate byte images against GNU C, using sizes
 * that cross the x86 machine copy threshold, byte tails, and page bounds.
 * All cases have distinct source/destination storage and a self-copy. */
extern int puts(const char *);

#define P46_CASE(N)                                                          \
    struct P46Blob_##N { unsigned char bytes[N]; };                          \
    struct P46Frame_##N {                                                     \
        unsigned long before;                                                 \
        struct P46Blob_##N blob;                                              \
        unsigned long after;                                                  \
    };                                                                        \
    static int p46_case_##N(void) {                                           \
        struct P46Frame_##N source;                                           \
        struct P46Frame_##N destination;                                      \
        unsigned long index;                                                  \
        source.before = 0x12345678UL;                                        \
        source.after = 0x87654321UL;                                         \
        destination.before = 0x10203040UL;                                   \
        destination.after = 0x50607080UL;                                    \
        for (index = 0; index < N; ++index) {                                \
            source.blob.bytes[index] =                                        \
                (unsigned char)((index * 17UL + N) & 255UL);                  \
            destination.blob.bytes[index] = 0;                               \
        }                                                                     \
        destination.blob = source.blob;                                       \
        source.blob.bytes[0] ^= 0x5aU;                                       \
        destination.blob = destination.blob;                                  \
        for (index = 0; index < N; ++index) {                                \
            if (destination.blob.bytes[index] !=                              \
                (unsigned char)((index * 17UL + N) & 255UL))                  \
                return 1;                                                     \
        }                                                                     \
        if (source.before != 0x12345678UL ||                                \
            source.after != 0x87654321UL ||                                 \
            destination.before != 0x10203040UL ||                           \
            destination.after != 0x50607080UL)                              \
            return 2;                                                         \
        return 0;                                                             \
    }

P46_CASE(1)
P46_CASE(3)
P46_CASE(7)
P46_CASE(8)
P46_CASE(9)
P46_CASE(2048)
P46_CASE(4096)
P46_CASE(4097)
P46_CASE(16384)

int main(void) {
    if (p46_case_1() || p46_case_3() || p46_case_7() ||
        p46_case_8() || p46_case_9() || p46_case_2048() ||
        p46_case_4096() || p46_case_4097() || p46_case_16384())
        return 42;
    puts("P46_AGGREGATE_COPY=PASS");
    return 0;
}
