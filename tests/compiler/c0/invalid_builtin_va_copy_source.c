int bad_va_copy_source(int fixed, ...) {
    char *target;
    int source;

    __builtin_va_copy(target, source);
    return fixed;
}
