#include <stdarg.h>

long indirect_variadic_sink(long tag, ...)
{
    static const long expected[] = {11L, 13L, 17L, 19L, 23L, 29L, 31L, 37L, 41L};
    va_list arguments;
    unsigned long index;

    if (tag != 7L) {
        return 1L;
    }
    va_start(arguments, tag);
    for (index = 0UL; index < sizeof(expected) / sizeof(expected[0]); ++index) {
        if (va_arg(arguments, long) != expected[index]) {
            va_end(arguments);
            return (long)(index + 2UL);
        }
    }
    va_end(arguments);
    return 0L;
}
