/* Independently compiled GNU AAPCS64 callers and callees for the MiniC
 * x8 indirect-result ABI. The exact same pair of TUs is also built fully
 * with GNU as the reference executable. */
#include <stdio.h>

struct Three { unsigned long word[3]; };
struct Four { unsigned long word[4]; };
struct Eighteen { unsigned long word[18]; };
struct Nested { struct Three head; unsigned long tail[2]; };
struct Seventeen { unsigned char byte[17]; };
struct SixtyFour { unsigned char byte[64]; };
struct TwoThousandFortyEight { unsigned char byte[2048]; };
struct Two { unsigned long word[2]; };
struct Bits {
    unsigned long low : 17;
    unsigned long high : 31;
    unsigned long tail[2];
};

extern struct Three minic_three(unsigned long);
extern struct Four minic_four(unsigned long);
extern struct Eighteen minic_eighteen(unsigned long);
extern struct Three minic_nested_three(unsigned long);
extern struct Eighteen minic_nested_eighteen(unsigned long);
extern struct Three minic_branch_three(unsigned long, int);
extern struct Four minic_ten_arguments(unsigned long, unsigned long,
    unsigned long, unsigned long, unsigned long, unsigned long,
    unsigned long, unsigned long, unsigned long, unsigned long);
extern struct Nested minic_nested_record(unsigned long);
extern struct Seventeen minic_seventeen(unsigned char);
extern struct SixtyFour minic_sixty_four(unsigned char);
extern struct TwoThousandFortyEight minic_2048(unsigned long);
extern struct TwoThousandFortyEight minic_nested_2048(unsigned long);
extern struct Two minic_two(unsigned long);
extern struct Three minic_dynamic_three(unsigned long);
extern struct Eighteen minic_roundtrip_eighteen(struct Eighteen, unsigned long);
extern struct Three minic_record_argument_and_result(struct Three, unsigned long);
extern struct Three minic_two_gnu_returns(unsigned long);
extern struct Three minic_direct_return_gnu_result(unsigned long);
extern struct Bits minic_bits(unsigned long);

/* The GNU object is deliberately NOT built by MiniC. Physical x8 destruction
 * demonstrates why a MiniC callee returning a large record must remember
 * its own caller's pointer rather than trusting x8 after any BL. */
unsigned long gnu_tap(unsigned long value) {
#if defined(__aarch64__)
    __asm__ volatile("mov x8, xzr" ::: "x8", "memory");
#else
    __asm__ volatile("" ::: "memory");
#endif
    return value + 19UL;
}

struct Three gnu_make_three(unsigned long seed) {
    struct Three result;
    result.word[0] = seed + 101UL;
    result.word[1] = seed + 102UL;
    result.word[2] = seed + 103UL;
    return result;
}

struct Eighteen gnu_make_eighteen(unsigned long seed) {
    struct Eighteen result;
    unsigned long i;
    for (i = 0UL; i < 18UL; ++i)
        result.word[i] = seed + i * 13UL;
    return result;
}

struct TwoThousandFortyEight gnu_make_2048(unsigned long seed) {
    struct TwoThousandFortyEight result;
    unsigned long i;
    for (i = 0UL; i < 2048UL; ++i)
        result.byte[i] = (unsigned char)(seed + i * 5UL);
    return result;
}

static int verify_2048(struct TwoThousandFortyEight value,
                       unsigned long seed, int nested) {
    unsigned long i;
    for (i = 0UL; i < 2048UL; ++i) {
        unsigned char expected = (unsigned char)(seed + i *
            (nested ? 5UL : 1UL));
        if (nested && i == 0UL) expected = (unsigned char)(expected + 1U);
        if (nested && i == 2047UL) expected = (unsigned char)(expected + 2U);
        if (value.byte[i] != expected) return 0;
    }
    return 1;
}

static int verify_byte_records(unsigned char seed) {
    struct Seventeen small = minic_seventeen(seed);
    struct SixtyFour medium = minic_sixty_four(seed);
    unsigned long i;
    for (i = 0UL; i < 17UL; ++i)
        if (small.byte[i] != (unsigned char)(seed + i)) return 0;
    for (i = 0UL; i < 64UL; ++i)
        if (medium.byte[i] !=
            (unsigned char)(seed ^ (unsigned char)(i * 3UL))) return 0;
    return 1;
}

static int verify_three(struct Three value, unsigned long a,
                        unsigned long b, unsigned long c) {
    return value.word[0] == a && value.word[1] == b && value.word[2] == c;
}

static int verify_four(struct Four value, unsigned long a, unsigned long b,
                       unsigned long c, unsigned long d) {
    return value.word[0] == a && value.word[1] == b &&
           value.word[2] == c && value.word[3] == d;
}

/* Always compare every byte-bearing word, including the final 144-byte
 * slot. A truncated 1/2-register result often passes shallow first-word
 * probes while silently losing the rest of this Linux-shaped record. */
static int verify_eighteen(struct Eighteen value, unsigned long seed,
                           unsigned long stride) {
    unsigned long i;
    for (i = 0UL; i < 18UL; ++i)
        if (value.word[i] != seed + i * stride)
            return 0;
    return 1;
}

int main(void) {
    struct Three a;
    struct Four b;
    struct Eighteen c;
    struct Nested d;
    struct Two two;
    struct Bits bits;

    a = minic_three(41UL);
    if (!verify_three(a, 41UL, 48UL, 41UL ^ 0x1234UL)) return 1;

    b = minic_four(91UL);
    if (!verify_four(b, 91UL, 92UL, 93UL, 94UL)) return 2;

    c = minic_eighteen(10UL);
    if (!verify_eighteen(c, 10UL, 17UL)) return 3;

    a = minic_nested_three(71UL);
    if (!verify_three(a, 172UL, 192UL, 174UL)) return 4;

    c = minic_nested_eighteen(51UL);
    if (c.word[0] != 70UL || c.word[17] != 51UL + 17UL * 13UL + 19UL)
        return 5;
    for (unsigned long i = 1UL; i < 17UL; ++i)
        if (c.word[i] != 51UL + i * 13UL) return 6;

    a = minic_branch_three(11UL, 0);
    if (!verify_three(a, 11UL, 12UL, 32UL)) return 7;
    a = minic_branch_three(11UL, 1);
    if (!verify_three(a, 11UL, 31UL, 13UL)) return 8;

    b = minic_ten_arguments(1UL, 2UL, 3UL, 4UL, 5UL,
                           6UL, 7UL, 8UL, 9UL, 10UL);
    if (!verify_four(b, 6UL, 15UL, 24UL, 10UL)) return 9;

    d = minic_nested_record(29UL);
    if (d.head.word[0] != 29UL || d.head.word[1] != 30UL ||
        d.head.word[2] != 31UL || d.tail[0] != 32UL ||
        d.tail[1] != 33UL) return 10;

    if (!verify_byte_records(9U)) return 11;
    if (!verify_byte_records(247U)) return 12;

    if (!verify_2048(minic_2048(23UL), 23UL, 0)) return 13;
    if (!verify_2048(minic_nested_2048(29UL), 29UL, 1)) return 14;

    two = minic_two(200UL);
    if (two.word[0] != 200UL || two.word[1] != 201UL) return 15;

    a = minic_dynamic_three(12UL);
    if (!verify_three(a, 113UL, 114UL, 127UL)) return 16;

    c = gnu_make_eighteen(19UL);
    c = minic_roundtrip_eighteen(c, 7UL);
    if (c.word[0] != 26UL ||
        c.word[17] != 19UL + 17UL * 13UL + 7UL) return 17;
    for (unsigned long i = 1UL; i < 17UL; ++i)
        if (c.word[i] != 19UL + i * 13UL) return 18;

    a = gnu_make_three(29UL);
    a = minic_record_argument_and_result(a, 7UL);
    if (!verify_three(a, 137UL, 138UL, 139UL)) return 19;

    a = minic_two_gnu_returns(13UL);
    if (!verify_three(a, 215UL, 332UL, 217UL)) return 20;

    a = minic_direct_return_gnu_result(91UL);
    if (!verify_three(a, 192UL, 193UL, 194UL)) return 21;

    bits = minic_bits(0x12345UL);
    if (bits.low != 0x12345UL ||
        bits.high != 3UL * 0x12345UL ||
        bits.tail[0] != 0x12345UL + 101UL ||
        bits.tail[1] != 0x12345UL + 102UL) return 22;

    puts("AAPCS64_INDIRECT_RECORD_RETURN=PASS sizes=17,24,32,40,64,144,2048"
         " GNU_MiniC_directions=2 nested_BL=1 x8_clobber=1"
         " return_paths=2 spilled_args=10 dynamic_sp=1"
         " incoming_record=2 bitfields=1 direct_return=1");
    return 42;
}
