/* SysV AMD64: an all-INTEGER 12-byte record occupies two GP eightbytes,
 * but the second contains only four valid object bytes.  These functions
 * deliberately cross the translation-unit ABI boundary in both directions. */
typedef unsigned int u32;
typedef unsigned long long u64;

typedef struct ThreeWords {
    u32 words[3];
} ThreeWords;

typedef struct ThreeFields {
    u32 first;
    u32 second;
    u32 third;
} ThreeFields;

extern ThreeWords gnu_three_words(u32 a, u32 b, u32 c);
extern u64 gnu_five_plus_three(u64 a, u64 b, u64 c, u64 d, u64 e,
                                ThreeWords value);

ThreeWords minic_three_words(ThreeWords value) {
    value.words[0] += 2U;
    value.words[1] += 3U;
    value.words[2] += 5U;
    return value;
}

ThreeFields minic_three_fields(ThreeFields value) {
    value.first += 7U;
    value.second += 11U;
    value.third += 13U;
    return value;
}

/* Five integer arguments consume all but one SysV GP register, so the
 * indivisible two-eightbyte record must occupy two outgoing stack slots. */
u64 minic_five_plus_three(u64 a, u64 b, u64 c, u64 d, u64 e,
                          ThreeWords value) {
    return a + b + c + d + e + value.words[0] * 7ULL +
           value.words[1] * 11ULL + value.words[2] * 13ULL;
}

u64 minic_call_gnu(void) {
    ThreeWords input = gnu_three_words(41U, 43U, 47U);
    ThreeWords output = minic_three_words(input);
    return gnu_five_plus_three(3ULL, 5ULL, 7ULL, 11ULL, 13ULL, output);
}

ThreeWords minic_indirect_three_words(ThreeWords (*fn)(ThreeWords),
                                     ThreeWords value) {
    return fn(value);
}
