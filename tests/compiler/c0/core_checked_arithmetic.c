/* One typed overflow matrix: retain the historical result-width conversions
 * and all three size-helper control-flow contracts. */
#define DEFINE(op, type, suffix) \
    type checked_##op##_##suffix(type a, type b, type *result) { \
        return __builtin_##op##_overflow(a, b, result); \
    }
#define WIDTHS(op) \
    DEFINE(op, int, int) \
    DEFINE(op, long, long) \
    DEFINE(op, unsigned int, uint) \
    DEFINE(op, unsigned long, ulong)
WIDTHS(add)
WIDTHS(sub)
WIDTHS(mul)

static _Bool must_check(_Bool overflow) { return overflow; }
unsigned long size_add(unsigned long a, unsigned long b) {
    unsigned long result;
    return __builtin_add_overflow(a, b, &result) ? ~0UL : result;
}
unsigned long size_sub(unsigned long a, unsigned long b) {
    unsigned long result;
    if (a == ~0UL || b == ~0UL || __builtin_sub_overflow(a, b, &result))
        return ~0UL;
    return result;
}
unsigned long size_mul(unsigned long a, unsigned long b) {
    unsigned long result;
    return must_check(__builtin_mul_overflow(a, b, &result)) ? 99UL : result;
}
