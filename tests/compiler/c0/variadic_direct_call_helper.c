#include <stdarg.h>

int verify_variadic(int tag, ...) {
    va_list arguments;
    int first;
    int promoted_char;
    long wide;
    int *pointer;
    double precise;
    int expected;

    va_start(arguments, tag);
    first = va_arg(arguments, int);
    promoted_char = va_arg(arguments, int);
    wide = va_arg(arguments, long);
    pointer = va_arg(arguments, int *);
    precise = va_arg(arguments, double);

    if (tag != 5 || first != 11 || promoted_char != 7 || wide != 1234 || pointer == 0 ||
        *pointer != 29 || precise != 2.5) {
        va_end(arguments);
        return 1;
    }
    for (expected = 61; expected <= 74; ++expected) {
        if (va_arg(arguments, int) != expected) {
            va_end(arguments);
            return 2;
        }
    }
    va_end(arguments);
    return 0;
}
