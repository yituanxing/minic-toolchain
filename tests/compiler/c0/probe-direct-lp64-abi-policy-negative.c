/* AAPCS64 has a fixed FP bank; the current qualified LoongArch64 linear
 * varargs cursor deliberately rejects this fixed-double function shape. */
unsigned long abi_named_double_variadic(double base, ...) {
    __builtin_va_list args;
    unsigned long next;
    __builtin_va_start(args, base);
    next = __builtin_va_arg(args, unsigned long);
    __builtin_va_end(args);
    return (unsigned long)base + next;
}
