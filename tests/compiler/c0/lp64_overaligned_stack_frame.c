/* All AAPCS64/LP64D callers guarantee only 16B SP alignment. A local type
 * aligned(32) must be placed at a genuinely 32B address, and a ninth GP
 * argument must still load from the ORIGINAL incoming call stack. */
struct Aligned32 {
    unsigned long words[4];
} __attribute__((aligned(32)));

unsigned long overalign_with_stack_argument(
    unsigned long a, unsigned long b, unsigned long c, unsigned long d,
    unsigned long e, unsigned long f, unsigned long g, unsigned long h,
    unsigned long ninth) {
    struct Aligned32 value;
    value.words[0] = ninth;
    value.words[1] = a + b + c;
    value.words[2] = d + e + f;
    value.words[3] = g + h;
    return value.words[0] + value.words[1] + value.words[2] + value.words[3];
}

unsigned long overalign_dynamic_stack(unsigned long amount) {
    struct Aligned32 value;
    unsigned char *scratch = __builtin_alloca(amount + 16UL);
    scratch[0] = (unsigned char)amount;
    value.words[0] = amount;
    value.words[1] = scratch[0];
    value.words[2] = amount + 1UL;
    value.words[3] = amount + 2UL;
    return value.words[0] + value.words[1] + value.words[2] + value.words[3];
}
