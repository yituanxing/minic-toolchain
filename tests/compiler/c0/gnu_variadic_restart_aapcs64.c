typedef __builtin_va_list MiniVaList;

extern int mini_va_take_one(MiniVaList ap);

int mini_va_restart(unsigned int tag, ...) {
    MiniVaList ap;
    int first;
    int second;
    __builtin_va_start(ap, tag);
    first = mini_va_take_one(ap);
    __builtin_va_end(ap);
    if (tag != 0U) {
        __builtin_va_start(ap, tag);
        second = mini_va_take_one(ap);
        __builtin_va_end(ap);
    } else {
        second = 0;
    }
    return first + second;
}
