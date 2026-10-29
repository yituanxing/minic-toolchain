/* AAPCS64 register-exhaustion oracle: one named GP argument occupies x0;
 * the eighth and ninth unnamed unsigned-long arguments must be fetched from
 * the CALLER's incoming stack, above the callee's x29/x30 frame record. */
#ifdef MINIC_AARCH64_CURSOR
/* The source AAPCS64 va_list is a 32-byte struct; Core passes its address to
 * the machine ABI cursor without inventing a pointer-valued C proxy. */
typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#define va_end(ap) __builtin_va_end(ap)
#else
#include <stdarg.h>
#endif

static unsigned long static_stack_cursor(unsigned long named, ...) {
    va_list ap;
    unsigned long values[9];
    unsigned int i;
    va_start(ap, named);
    for (i = 0; i < 9; ++i) values[i] = va_arg(ap, unsigned long);
    va_end(ap);
    return values[0] + values[6] + values[7] * 100UL + values[8];
}

/* Force both a dynamic x19-anchored frame and a >16-byte local alignment.
 * The over-aligned case must recover the saved entry-SP value, not derive
 * incoming arguments from the post-realignment frame anchor. */
static unsigned long aligned_dynamic_stack_cursor(unsigned long named, ...) {
    struct __attribute__((aligned(64))) Aligned {
        volatile unsigned char bytes[64];
    } local;
    volatile unsigned char *dynamic =
        (volatile unsigned char *)__builtin_alloca((named & 31UL) + 32UL);
    va_list ap;
    unsigned long values[9];
    unsigned int i;
    local.bytes[0] = (unsigned char)named;
    dynamic[0] = local.bytes[0];
    va_start(ap, named);
    for (i = 0; i < 9; ++i) values[i] = va_arg(ap, unsigned long);
    va_end(ap);
    return values[0] + values[6] + values[7] * 100UL + values[8] +
           (unsigned long)(dynamic[0] - local.bytes[0]);
}

/* Nine named GP scalars consume x0..x7 and one incoming stack slot. The
 * first unnamed argument is at entry-SP+16+8, not entry-SP+16. */
static unsigned long named_gp_stack_cursor(
    unsigned long g0, unsigned long g1, unsigned long g2,
    unsigned long g3, unsigned long g4, unsigned long g5,
    unsigned long g6, unsigned long g7, unsigned long g8, ...) {
    va_list ap;
    unsigned long first, second;
    va_start(ap, g8);
    first = va_arg(ap, unsigned long);
    second = va_arg(ap, unsigned long);
    va_end(ap);
    return first * 100UL + second;
}

/* Two fixed 16-byte records occupy x0/x1 and x2/x3. Counting their C
 * parameters as only two GPRs would start va_arg at x2 instead of x4. */
struct TwoLongs { unsigned long lo; unsigned long hi; };
static unsigned long named_record_gp_cursor(
    struct TwoLongs first_fixed, struct TwoLongs second_fixed, ...) {
    va_list ap;
    unsigned long first, second;
    va_start(ap, second_fixed);
    first = va_arg(ap, unsigned long);
    second = va_arg(ap, unsigned long);
    va_end(ap);
    return first * 100UL + second;
}

/* The saved entry SP remains authoritative even when the 9th fixed
 * integer travels in a caller stack slot and the callee realigns SP. */
static unsigned long named_aligned_dynamic_stack_cursor(
    unsigned long g0, unsigned long g1, unsigned long g2,
    unsigned long g3, unsigned long g4, unsigned long g5,
    unsigned long g6, unsigned long g7, unsigned long g8, ...) {
    struct __attribute__((aligned(64))) Aligned {
        volatile unsigned char bytes[64];
    } local;
    volatile unsigned char *dynamic =
        (volatile unsigned char *)__builtin_alloca((g0 & 31UL) + 32UL);
    va_list ap;
    unsigned long first, second;
    local.bytes[0] = (unsigned char)g8;
    dynamic[0] = local.bytes[0];
    va_start(ap, g8);
    first = va_arg(ap, unsigned long);
    second = va_arg(ap, unsigned long);
    va_end(ap);
    return first * 100UL + second +
           (unsigned long)(dynamic[0] - local.bytes[0]);
}

/* Both fixed banks overflow: g8 is in the first named stack slot and f8
 * the second. The unnamed integer and double follow in distinct slots. */
static unsigned long named_mixed_stack_cursor(
    unsigned long g0, unsigned long g1, unsigned long g2,
    unsigned long g3, unsigned long g4, unsigned long g5,
    unsigned long g6, unsigned long g7, unsigned long g8,
    double f0, double f1, double f2, double f3, double f4,
    double f5, double f6, double f7, double f8, ...) {
    va_list ap;
    unsigned long integer;
    double fractional;
    va_start(ap, f8);
    integer = va_arg(ap, unsigned long);
    fractional = va_arg(ap, double);
    va_end(ap);
    return integer + (unsigned long)(fractional * 10.0);
}

int main(void) {
    struct TwoLongs fixed0 = {11UL, 22UL};
    struct TwoLongs fixed1 = {33UL, 44UL};
    const unsigned long expected = 1UL + 7UL + 8UL * 100UL + 9UL;
    if (static_stack_cursor(99UL, 1UL, 2UL, 3UL, 4UL, 5UL, 6UL,
                            7UL, 8UL, 9UL) != expected) return 1;
    if (aligned_dynamic_stack_cursor(99UL, 1UL, 2UL, 3UL, 4UL, 5UL,
                                     6UL, 7UL, 8UL, 9UL) != expected) return 2;
    if (named_gp_stack_cursor(10UL, 11UL, 12UL, 13UL, 14UL, 15UL, 16UL,
                              17UL, 18UL, 23UL, 45UL) != 2345UL) return 3;
    if (named_record_gp_cursor(fixed0, fixed1, 23UL, 45UL) != 2345UL)
        return 6;
    if (named_aligned_dynamic_stack_cursor(
            10UL, 11UL, 12UL, 13UL, 14UL, 15UL, 16UL,
            17UL, 18UL, 23UL, 45UL) != 2345UL) return 5;
    if (named_mixed_stack_cursor(10UL, 11UL, 12UL, 13UL, 14UL, 15UL,
                                 16UL, 17UL, 18UL,
                                 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0,
                                 8.0, 9.0, 1234UL, 2.5) != 1259UL) return 4;
    return 0;
}
