/* Original Linux 6.6 atomic_lse.h GNU operand and AArch64 ISA contracts.
 * These are privileged kernel-oriented atomics: compile/assemble only. */
typedef unsigned __int128 minic_u128;
typedef union {
    minic_u128 full;
    struct { unsigned long low, high; } halves;
} minic_pair;

#define MINIC_LSE_CASP(NAME, ORDER, CLOBBERS...)                           \
minic_u128 NAME(volatile minic_u128 *ptr, minic_u128 expected,            \
                minic_u128 replacement) {                                 \
    minic_pair o = { .full = expected }, n = { .full = replacement };      \
    register unsigned long x0 asm("x0") = o.halves.low;                   \
    register unsigned long x1 asm("x1") = o.halves.high;                  \
    register unsigned long x2 asm("x2") = n.halves.low;                   \
    register unsigned long x3 asm("x3") = n.halves.high;                  \
    register unsigned long x4 asm("x4") = (unsigned long)ptr;             \
    asm volatile(".arch_extension lse\n"                               \
        "casp" ORDER "\t%[old1], %[old2], %[new1], %[new2], %[v]\n"     \
        : [old1] "+&r" (x0), [old2] "+&r" (x1),                          \
          [v] "+Q" (*(minic_u128 *)ptr)                                  \
        : [new1] "r" (x2), [new2] "r" (x3), [ptr] "r" (x4),               \
          [oldval1] "r" (o.halves.low), [oldval2] "r" (o.halves.high)     \
        : CLOBBERS);                                                      \
    minic_pair ret; ret.halves.low = x0; ret.halves.high = x1;            \
    return ret.full;                                                      \
}

#ifndef MINIC_ONLY_DEC
MINIC_LSE_CASP(minic_lse_casp128, "")
MINIC_LSE_CASP(minic_lse_casp128_mb, "al", "memory")
#endif

typedef struct { long counter; } minic_atomic64;

#ifndef MINIC_ONLY_CASP
long minic_lse_dec_if_positive(minic_atomic64 *v) {
    unsigned long tmp;
    asm volatile(
        ".arch_extension lse\n"
        "1:\tldr\t%x[tmp], %[v]\n"
        "\tsubs\t%[ret], %x[tmp], #1\n"
        "\tb.lt\t2f\n"
        "\tcasal\t%x[tmp], %[ret], %[v]\n"
        "\tsub\t%x[tmp], %x[tmp], #1\n"
        "\tsub\t%x[tmp], %x[tmp], %[ret]\n"
        "\tcbnz\t%x[tmp], 1b\n"
        "2:"
        : [ret] "+&r" (v), [v] "+Q" (v->counter), [tmp] "=&r" (tmp)
        :
        : "cc", "memory");
    return (long)v;
}
#endif
