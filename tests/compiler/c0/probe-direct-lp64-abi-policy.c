/* AAPCS64 and LoongArch LP64D share Core CALL semantics, but differ in
 * aggregate/GPR splitting, aligned __int128 pairs and hidden result slots. */
struct AbiPair { unsigned long low, high; };
struct AbiLarge { unsigned long first, middle, last; };

extern unsigned long abi_pair_callee(
    unsigned long, unsigned long, unsigned long, unsigned long,
    unsigned long, unsigned long, unsigned long, struct AbiPair);
extern unsigned long abi_wide_callee(
    unsigned long, unsigned long, unsigned long, unsigned long,
    unsigned long, unsigned long, unsigned long, unsigned __int128);
extern struct AbiLarge abi_large_callee(struct AbiLarge, unsigned long);
extern double abi_float_callee(unsigned long, double, float);

unsigned long abi_pair_boundary(struct AbiPair pair) {
    return abi_pair_callee(1UL, 2UL, 3UL, 4UL, 5UL, 6UL, 7UL, pair);
}

unsigned long abi_wide_boundary(unsigned __int128 wide) {
    return abi_wide_callee(1UL, 2UL, 3UL, 4UL, 5UL, 6UL, 7UL, wide);
}

struct AbiLarge abi_indirect_return(struct AbiLarge value) {
    return abi_large_callee(value, 5UL);
}

double abi_fp_boundary(double value) {
    return abi_float_callee(3UL, value, 4.0f);
}

unsigned long abi_linear_or_state_va(unsigned long tag, ...) {
    __builtin_va_list args;
    unsigned long left, right;
    __builtin_va_start(args, tag);
    left = __builtin_va_arg(args, unsigned long);
    right = __builtin_va_arg(args, unsigned long);
    __builtin_va_end(args);
    return tag + left + right;
}

unsigned long abi_variadic_boundary(void) {
    return abi_linear_or_state_va(1UL, 2UL, 3UL);
}
