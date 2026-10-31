/* GNU side of the cross-compiler ABI oracle.  The same driver must pass
 * when the subject is GNU-built and when it is MiniC-built. */
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

_Static_assert(sizeof(ThreeWords) == 12U, "SysV 12-byte record size");
_Static_assert(_Alignof(ThreeWords) == 4U, "SysV 12-byte record alignment");
_Static_assert(sizeof(ThreeFields) == 12U, "SysV 12-byte field record size");

extern ThreeWords minic_three_words(ThreeWords value);
extern ThreeFields minic_three_fields(ThreeFields value);
extern u64 minic_five_plus_three(u64, u64, u64, u64, u64, ThreeWords);
extern u64 minic_call_gnu(void);
extern ThreeWords minic_indirect_three_words(ThreeWords (*fn)(ThreeWords),
                                              ThreeWords value);

ThreeWords gnu_three_words(u32 a, u32 b, u32 c) {
    ThreeWords result = {{a, b, c}};
    return result;
}

u64 gnu_five_plus_three(u64 a, u64 b, u64 c, u64 d, u64 e,
                         ThreeWords value) {
    return a + b + c + d + e + value.words[0] * 7ULL +
           value.words[1] * 11ULL + value.words[2] * 13ULL;
}

int main(void) {
    ThreeWords input = {{17U, 19U, 23U}};
    ThreeWords output;
    ThreeFields fields = {17U, 19U, 23U};
    ThreeFields result_fields;
    struct WithCanary {
        ThreeWords value;
        u32 guard;
    } protected_result = {{{0U, 0U, 0U}}, 0xBADC0FFEU};

    output = minic_three_words(input);
    if (output.words[0] != 19U || output.words[1] != 22U ||
        output.words[2] != 28U)
        return 1;
    result_fields = minic_three_fields(fields);
    if (result_fields.first != 24U || result_fields.second != 30U ||
        result_fields.third != 36U)
        return 6;
    if (minic_five_plus_three(2ULL, 3ULL, 5ULL, 7ULL, 11ULL, input) !=
        gnu_five_plus_three(2ULL, 3ULL, 5ULL, 7ULL, 11ULL, input))
        return 2;
    if (minic_call_gnu() != gnu_five_plus_three(3ULL, 5ULL, 7ULL, 11ULL,
                                               13ULL, minic_three_words(
                                                   gnu_three_words(41U, 43U, 47U))))
        return 3;
    output = minic_indirect_three_words(minic_three_words, input);
    if (output.words[0] != 19U || output.words[1] != 22U ||
        output.words[2] != 28U)
        return 4;
    protected_result.value = minic_three_words(input);
    if (protected_result.guard != 0xBADC0FFEU ||
        protected_result.value.words[2] != 28U)
        return 5;
    return 0;
}
