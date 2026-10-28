/* AAPCS64 large aggregate results use the independent x8 result address.
 * These functions are compiled by MiniC and called from a GNU-built caller.
 * Some make nested GNU calls that clobber x8 before their own return. */

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

extern struct Three gnu_make_three(unsigned long);
extern struct Eighteen gnu_make_eighteen(unsigned long);
extern unsigned long gnu_tap(unsigned long);
extern struct TwoThousandFortyEight gnu_make_2048(unsigned long);

struct Three minic_three(unsigned long seed) {
    struct Three result;
    result.word[0] = seed;
    result.word[1] = seed + 7UL;
    result.word[2] = seed ^ 0x1234UL;
    return result;
}

struct Four minic_four(unsigned long seed) {
    struct Four result;
    result.word[0] = seed;
    result.word[1] = seed + 1UL;
    result.word[2] = seed + 2UL;
    result.word[3] = seed + 3UL;
    return result;
}

struct Eighteen minic_eighteen(unsigned long seed) {
    struct Eighteen result;
    unsigned long i;
    for (i = 0UL; i < 18UL; ++i)
        result.word[i] = seed + i * 17UL;
    return result;
}

struct Three minic_nested_three(unsigned long seed) {
    struct Three result = gnu_make_three(seed);
    result.word[1] = gnu_tap(result.word[1]);
    return result;
}

struct Eighteen minic_nested_eighteen(unsigned long seed) {
    struct Eighteen result = gnu_make_eighteen(seed);
    result.word[0] = gnu_tap(result.word[0]);
    result.word[17] = gnu_tap(result.word[17]);
    return result;
}

/* Exercise two different return blocks and the address lifetime across a
 * nested call; the copy destination must always be the ORIGINAL caller's x8. */
struct Three minic_branch_three(unsigned long seed, int which) {
    struct Three result;
    result.word[0] = seed;
    result.word[1] = seed + 1UL;
    result.word[2] = seed + 2UL;
    if (which) {
        result.word[1] = gnu_tap(result.word[1]);
        return result;
    }
    result.word[2] = gnu_tap(result.word[2]);
    return result;
}

/* Normal integer call arguments must stay in x0..x7 (plus the caller stack)
 * even though the hidden result pointer lives independently in x8. */
struct Four minic_ten_arguments(unsigned long a, unsigned long b,
    unsigned long c, unsigned long d, unsigned long e, unsigned long f,
    unsigned long g, unsigned long h, unsigned long i, unsigned long j) {
    struct Four result;
    result.word[0] = a + b + c;
    result.word[1] = d + e + f;
    result.word[2] = g + h + i;
    result.word[3] = j;
    return result;
}

struct Nested minic_nested_record(unsigned long seed) {
    struct Nested result;
    result.head.word[0] = seed;
    result.head.word[1] = seed + 1UL;
    result.head.word[2] = seed + 2UL;
    result.tail[0] = seed + 3UL;
    result.tail[1] = seed + 4UL;
    return result;
}

/* Boundary 17 bytes: the third byte beyond the first two eightbytes is
 * material, despite the object having alignment one and no 64-bit member. */
struct Seventeen minic_seventeen(unsigned char seed) {
    struct Seventeen result;
    unsigned long i;
    for (i = 0UL; i < 17UL; ++i)
        result.byte[i] = (unsigned char)(seed + (unsigned char)i);
    return result;
}

/* Byte-only composite with no integer GP eightbyte classification. This
 * must be copied into x8, not reconstructed as guessed x0..x7 chunks. */
struct SixtyFour minic_sixty_four(unsigned char seed) {
    struct SixtyFour result;
    unsigned long i;
    for (i = 0UL; i < 64UL; ++i)
        result.byte[i] = (unsigned char)(seed ^ (unsigned char)(i * 3UL));
    return result;
}

/* Keep the largest currently qualified record-size boundary in the test.
 * One counted AArch64 copy of 2048 bytes is required; emitting a giant
 * unrolled instruction template is a compiler code-size regression. */
struct TwoThousandFortyEight minic_2048(unsigned long seed) {
    struct TwoThousandFortyEight result;
    unsigned long i;
    for (i = 0UL; i < 2048UL; ++i)
        result.byte[i] = (unsigned char)(seed + i);
    return result;
}

/* The callee returns the 2048-byte buffer through the same independent
 * AAPCS64 x8 register, while the caller must preserve its own x8 for the
 * final return. The last byte detects truncated copies. */
struct TwoThousandFortyEight minic_nested_2048(unsigned long seed) {
    struct TwoThousandFortyEight result = gnu_make_2048(seed);
    result.byte[0] = (unsigned char)(result.byte[0] + 1U);
    result.byte[2047] = (unsigned char)(result.byte[2047] + 2U);
    return result;
}

/* Another conventional integer result must not accidentally receive x8:
 * two eightbytes stay in x0/x1, adjacent to the indirect-result functions. */
struct Two minic_two(unsigned long seed) {
    struct Two result;
    result.word[0] = seed;
    result.word[1] = seed + 1UL;
    return result;
}

/* Dynamic stack movement changes sp after the prologue. Use the stable
 * callee-saved x19 anchor to reload the original x8 and to locate the
 * result object after a nested GNU function call. */
struct Three minic_dynamic_three(unsigned long seed) {
    volatile unsigned char *scratch =
        (volatile unsigned char *)__builtin_alloca((seed & 15UL) + 48UL);
    struct Three result = gnu_make_three(seed);
    scratch[0] = (unsigned char)seed;
    result.word[2] += (unsigned long)scratch[0];
    return result;
}

/* In AAPCS64 a 144-byte parameter is passed by reference through x0 while
 * the 144-byte return is written through x8. These addresses must stay
 * independent even when the function mutates its private parameter copy. */
struct Eighteen minic_roundtrip_eighteen(
    struct Eighteen incoming, unsigned long increment) {
    incoming.word[0] += increment;
    incoming.word[17] += increment;
    return incoming;
}

struct Three minic_record_argument_and_result(
    struct Three incoming, unsigned long increment) {
    incoming.word[0] += increment;
    incoming.word[1] += increment;
    incoming.word[2] += increment;
    return incoming;
}

/* Exercise two independent GNU result buffers and ensure that the final
 * function's own x8 is not replaced by either nested direct-call x8. */
struct Three minic_two_gnu_returns(unsigned long seed) {
    struct Three first = gnu_make_three(seed);
    struct Three second = gnu_make_three(first.word[0]);
    second.word[1] += first.word[2];
    return second;
}

/* Return a GNU record call's rvalue directly, without a named C lvalue.
 * Core must materialize the call's object before the final byte copy. */
struct Three minic_direct_return_gnu_result(unsigned long seed) {
    return gnu_make_three(seed);
}

/* Integer bit-fields inside an over-16-byte record remain a memory result.
 * Raw bytes, not piecemeal shifted GP registers, preserve GNU layout. */
struct Bits minic_bits(unsigned long seed) {
    struct Bits result;
    result.low = seed & 0x1ffffUL;
    result.high = (seed * 3UL) & 0x7fffffffUL;
    result.tail[0] = seed + 101UL;
    result.tail[1] = seed + 102UL;
    return result;
}
