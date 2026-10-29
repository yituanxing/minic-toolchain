/* Compiled twice, once by GNU C and once by MiniC. Check both operand orders.
 * Use real function arguments: the compiler must not fold the test constants. */
#ifndef MIXED_FORWARD
#define MIXED_FORWARD gnu_mixed_forward
#endif
#ifndef MIXED_REVERSE
#define MIXED_REVERSE gnu_mixed_reverse
#endif

int MIXED_FORWARD(unsigned long large, int narrow, int *out) {
    return __builtin_add_overflow(large, narrow, out);
}

int MIXED_REVERSE(int narrow, unsigned long large, int *out) {
    return __builtin_add_overflow(narrow, large, out);
}

#ifdef MIXED_UNSUPPORTED
/* Signed 64-bit + signed 32-bit → signed 32-bit needs a separate proof;
 * do not admit it by silently treating either operand as unsigned64. */
int unsupported_mixed_add(long large, int narrow, int *out) {
    return __builtin_add_overflow(large, narrow, out);
}
#endif
