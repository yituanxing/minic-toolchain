/* Host-preprocessed Core/Machine smoke for the RV32 zlib CI's mandatory
 * variadic preflight. GNU's <stdarg.h> maps these four operations to exactly
 * these builtin forms for the ILP32 target. The paired 64-bit constants and
 * two-word comparisons must survive typed MachineIR whole-function lowering. */
int rv32_ci_vwide(int fixed, ...) {
    __builtin_va_list ap;
    unsigned long long big;
    double dbl;
    int tail;
    __builtin_va_start(ap, fixed);
    big = __builtin_va_arg(ap, unsigned long long);
    dbl = __builtin_va_arg(ap, double);
    tail = __builtin_va_arg(ap, int);
    __builtin_va_end(ap);
    return fixed == 3 && big == 0x1122334455667788ULL &&
           dbl == 6.5 && tail == 9 ? 47 : 0;
}

int rv32_ci_vwide_spilled(int a, int b, int c, int d, int e,
                         int f, int g, ...) {
    __builtin_va_list ap;
    unsigned long long big;
    int tail;
    __builtin_va_start(ap, g);
    big = __builtin_va_arg(ap, unsigned long long);
    tail = __builtin_va_arg(ap, int);
    __builtin_va_end(ap);
    return a + b + c + d + e + f + g == 28 &&
           big == 0x123456789abcdef0ULL && tail == 9 ? 48 : 0;
}
