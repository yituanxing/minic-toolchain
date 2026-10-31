#include <stdarg.h>

/* Linux early exception handlers invoke integer-only variadic functions
 * before CR4.OSFXSR makes even an unused XMM spill legal. */
__attribute__((noinline))
unsigned long minic_x86_integer_varargs(unsigned long fixed, ...) {
    va_list args;
    unsigned long first, second;
    va_start(args, fixed);
    first = va_arg(args, unsigned long);
    second = va_arg(args, unsigned long);
    va_end(args);
    return fixed + first * 3UL + second;
}

#ifdef MINIC_X86_TEST_FLOAT_VARARGS
__attribute__((noinline))
double minic_x86_float_varargs(unsigned long fixed, ...) {
    va_list args;
    double first;
    va_start(args, fixed);
    first = va_arg(args, double);
    va_end(args);
    return first + (double)fixed;
}
#endif

int main(void) {
    if (minic_x86_integer_varargs(2UL, 7UL, 11UL) != 34UL)
        return 1;
#ifdef MINIC_X86_TEST_FLOAT_VARARGS
    if (minic_x86_float_varargs(2UL, 3.5) != 5.5)
        return 2;
#endif
    return 0;
}
