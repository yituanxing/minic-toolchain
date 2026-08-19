typedef __builtin_va_list va_list;

static int bad_va_start(int first, int last, ...) {
    va_list args;
    __builtin_va_start(args, first);
    __builtin_va_end(args);
    return last;
}
