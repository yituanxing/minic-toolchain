#include <stdarg.h>

int verify_variadic(int tag, ...)
{
    va_list arguments;
    int first;
    int promoted_char;
    long wide;
    int *pointer;
    double precise;

    va_start(arguments, tag);
    first = va_arg(arguments, int);
    promoted_char = va_arg(arguments, int);
    wide = va_arg(arguments, long);
    pointer = va_arg(arguments, int *);
    precise = va_arg(arguments, double);
    va_end(arguments);

    if (tag != 5) {
        return 1;
    }
    if (first != 11) {
        return 2;
    }
    if (promoted_char != 7) {
        return 3;
    }
    if (wide != 1234) {
        return 4;
    }
    if (pointer == 0 || *pointer != 29) {
        return 5;
    }
    if (precise != 2.5) {
        return 6;
    }
    return 0;
}
