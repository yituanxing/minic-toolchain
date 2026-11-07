/* Minimal exact GNU-preprocessed Linux 6.6.143 VDSO vma.i branch shape:
 * get_random_u32_below() has a __builtin_constant_p early-return, followed
 * by a compiletime-assert body that is unreachable for a variable ceil.
 * The x86 emitter must not preserve this unreachable unresolved reference. */
typedef unsigned int u32;

u32 __get_random_u32_below(u32 ceil) {
    return ceil + 4U;
}

static inline __attribute__((__gnu_inline__)) __attribute__((__unused__))
u32 get_random_u32_below(u32 ceil)
{
    if (!__builtin_constant_p(ceil))
        return __get_random_u32_below(ceil);
    do {
        __attribute__((__noreturn__)) extern void __compiletime_assert_80(void)
            __attribute__((__error__("get_random_u32_below() must take ceil > 0")));
        if (!(!(!ceil))) __compiletime_assert_80();
    } while (0);
    if (ceil <= 1)
        return 0;
    return 7;
}

int main(int argc, char **argv) {
    (void)argv;
    return get_random_u32_below((u32)argc) == (u32)argc + 4U ? 42 : 1;
}
