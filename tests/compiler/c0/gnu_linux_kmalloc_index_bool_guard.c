/* Linux 6.6.143 include/linux/slab.h::__kmalloc_index, copied from the
 * original x86 GNU -E frozen vma.i (the 2nd bool argument controls a final
 * compile-time error only after all size-range returns were exhausted).
 * Keep the GNU diagnostic call undefined: no fallback or fake definition. */
typedef __SIZE_TYPE__ size_t;
typedef _Bool bool;

static inline __attribute__((__gnu_inline__, __always_inline__, __unused__))
unsigned int __kmalloc_index(size_t size, bool size_is_constant)
{
    if (!size) return 0;
    if (size <= (1 << 3)) return 3;
    if ((1 << 3) <= 32 && size > 64 && size <= 96) return 1;
    if ((1 << 3) <= 64 && size > 128 && size <= 192) return 2;
    if (size <= 8) return 3;
    if (size <= 16) return 4;
    if (size <= 32) return 5;
    if (size <= 64) return 6;
    if (size <= 128) return 7;
    if (size <= 256) return 8;
    if (size <= 512) return 9;
    if (size <= 1024) return 10;
    if (size <= 2 * 1024) return 11;
    if (size <= 4 * 1024) return 12;
    if (size <= 8 * 1024) return 13;
    if (size <= 16 * 1024) return 14;
    if (size <= 32 * 1024) return 15;
    if (size <= 64 * 1024) return 16;
    if (size <= 128 * 1024) return 17;
    if (size <= 256 * 1024) return 18;
    if (size <= 512 * 1024) return 19;
    if (size <= 1024 * 1024) return 20;
    if (size <= 2 * 1024 * 1024) return 21;
    if (!0 && size_is_constant) {
        __attribute__((__noreturn__)) extern void __compiletime_assert_212(void)
            __attribute__((__error__("unexpected size in kmalloc_index()")));
        if (!(!(1))) __compiletime_assert_212();
    } else {
        __builtin_unreachable();
    }
    return (unsigned int)-1;
}

/* The original inline source may see a constant second argument although
 * its first argument is merely range-bounded by an enclosing caller. */
#ifdef MINIC_TEST_RANGE_GUARD
unsigned int bound_size_512(size_t n)
{
    if (n > 512) return 0;
    return __kmalloc_index(n, 1);
}
#endif

/* Unknown size and a constant false second argument are fully valid. */
unsigned int dynamic_size(size_t n)
{
    return __kmalloc_index(n, __builtin_constant_p(n));
}

unsigned int known_size(void)
{
    return __kmalloc_index(64, 1);
}

/* The query may turn true when Core knows a local initializer.  No zero-fact
 * clone can be selected in that caller. */
unsigned int local_constant_size(void)
{
    size_t n = 64;
    return __kmalloc_index(n, __builtin_constant_p(n));
}

/* A genuinely unsafe constant-enabled unknown size MUST remain rejected.
 * The candidate-only zero clone may never be substituted into this call. */
#ifdef MINIC_TEST_UNSAFE_TRUE
unsigned int unsafe_true(size_t n)
{
    return __kmalloc_index(n, 1);
}
#endif

int main(int argc, char **argv)
{
    (void)argv;
    return dynamic_size((size_t)argc) == 3U &&
           known_size() == 6 && local_constant_size() == 6
           ? 42 : 1;
}
