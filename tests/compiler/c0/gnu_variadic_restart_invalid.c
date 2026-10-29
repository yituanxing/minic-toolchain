typedef __builtin_va_list MiniVaList;

int mini_invalid_start(unsigned int tag) {
    MiniVaList ap;
    __builtin_va_start(ap, tag);
    __builtin_va_end(ap);
    return 0;
}
