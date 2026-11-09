/* ABI cursor, mixed-size arguments, and direct/indirect variadic calls. */
typedef __builtin_va_list MirVaList;

volatile long mir_variadic_sink;

__attribute__((noinline))
long mir_variadic_sum(unsigned count, ...) {
    MirVaList ap;
    unsigned i;
    long sum = 0;
    __builtin_va_start(ap, count);
    for (i = 0U; i < count; ++i)
        sum += __builtin_va_arg(ap, long);
    __builtin_va_end(ap);
    mir_variadic_sink = sum;
    return sum;
}

typedef long (*MirVariadicPointer)(unsigned, ...);

__attribute__((noinline))
long mir_variadic_bridge(MirVariadicPointer p, long base) {
    return p(9U, base, base + 1, base + 2, base + 3,
             base + 4, base + 5, base + 6, base + 7, base + 8);
}

int main(void) {
    return (int)(mir_variadic_bridge(mir_variadic_sum, 10L) & 255L);
}
